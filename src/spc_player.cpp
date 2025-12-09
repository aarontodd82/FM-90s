#include "spc_player.h"
#include <Arduino.h>
#include "audio_system.h"  // For setFadeGain
#include "audio_globals.h"  // For persistent audio connections

// Include the C interface from blargg's library
#include "External/snes_spc/snes_spc/spc.h"
#include "External/snes_spc/snes_spc/SPC_Filter.h"

// Static instance for timer callback
SPCPlayer* SPCPlayer::instance_ = nullptr;

// Constructor
SPCPlayer::SPCPlayer(const PlayerConfig& config)
    : fileSource_(config.fileSource)
    , file_data_(nullptr)
    , file_size_(0)
    , spc_emu_(nullptr)
    , filter_(nullptr)
    , has_id666_(false)
    , state_(PlayerState::IDLE)
    , completionCallback_(nullptr)
    , samples_played_(0)
    , fade_start_sample_(0)
    , fade_length_samples_(0)
    , total_samples_(0)
    , mixerLeft_(config.mixerChannel1Left)   // Use submixer for SPC
    , mixerRight_(config.mixerChannel1Right)
    , mainMixerLeft_(config.mixerLeft)       // Main mixer for line-in control
    , mainMixerRight_(config.mixerRight)
    , fadeMixerLeft_(config.fadeMixerLeft)
    , fadeMixerRight_(config.fadeMixerRight)
    , ring_buffer_(nullptr)
    , write_pos_(0)
    , read_pos_(0)
    , audio_stream_(config.spcAudioStream) {  // Check if global exists

    Serial.println("[SPCPlayer] Initializing with PlayerConfig");

    // AudioStreamSPC is created globally in main.cpp (stays alive permanently)
    // Just wire this player instance to the global stream
    if (!audio_stream_) {
        Serial.println("[SPCPlayer] ERROR: audio_stream_ is null! Should be set by PlayerConfig");
        return;
    }

    Serial.printf("[SPCPlayer] Using global AudioStreamSPC at 0x%08X\n", (uint32_t)audio_stream_);
    Serial.printf("[SPCPlayer] Setting player pointer to this=0x%08X\n", (uint32_t)this);
    audio_stream_->setPlayer(this);

    // Set static instance for timer callback
    instance_ = this;

    // Create SPC emulator
    spc_emu_ = spc_new();
    if (!spc_emu_) {
        Serial.println("ERROR: Failed to create SPC emulator");
        return;
    }

    // Create filter for better sound quality
    filter_ = spc_filter_new();
    if (!filter_) {
        Serial.println("ERROR: Failed to create SPC filter");
        spc_delete(spc_emu_);
        spc_emu_ = nullptr;
        return;
    }

    // Set up filter for slight treble boost to compensate for Gaussian interpolation
    spc_filter_set_gain(filter_, spc_filter_gain_unit); // Normal gain

    // Allocate ring buffer
    ring_buffer_ = new int16_t[RING_BUFFER_SIZE * 2]; // Stereo
    if (!ring_buffer_) {
        Serial.println("ERROR: Failed to allocate ring buffer");
        spc_delete(spc_emu_);
        spc_filter_delete(filter_);
        spc_emu_ = nullptr;
        filter_ = nullptr;
        return;
    }

    // Clear ring buffer
    memset(ring_buffer_, 0, RING_BUFFER_SIZE * 2 * sizeof(int16_t));

    // Initialize resampler
    resampler_.position = 0.0f;
    resampler_.increment = SPC_SAMPLE_RATE / TEENSY_SAMPLE_RATE;
    resampler_.prev_l = 0;
    resampler_.prev_r = 0;

    // AudioConnections are created globally in main.cpp (stay alive permanently)
    // Mixer channel 1 is already muted at startup, shared with NES APU
    // SPCPlayer::play() will unmute when playing

    Serial.println("[SPCPlayer] Initialization complete");
}

// Destructor
SPCPlayer::~SPCPlayer() {
    Serial.println("[SPCPlayer] Destructor - shutting down");

    stop();

    // Unwire this player from the shared AudioStreamSPC
    if (audio_stream_) {
        audio_stream_->setPlayer(nullptr);
        Serial.println("[SPCPlayer] Disconnected from shared AudioStreamSPC");
    }

    // AudioStreamSPC is NOT owned by this player - don't delete it!
    // AudioConnections are persistent and global - don't disconnect them!

    if (ring_buffer_) {
        delete[] ring_buffer_;
        ring_buffer_ = nullptr;
    }

    if (filter_) {
        spc_filter_delete(filter_);
        filter_ = nullptr;
    }

    if (spc_emu_) {
        spc_delete(spc_emu_);
        spc_emu_ = nullptr;
    }

    if (file_data_) {
        delete[] file_data_;
        file_data_ = nullptr;
    }

    instance_ = nullptr;
    Serial.println("[SPCPlayer] Destructor complete");
}

// Load SPC file
bool SPCPlayer::loadFile(const char* path) {
    // // Serial.printf("[SPCPlayer] Loading file: %s\n", path);

    // Only stop if we're actually playing (don't mute if we're idle)
    if (state_ == PlayerState::PLAYING || state_ == PlayerState::PAUSED) {
        stop();

        // CRITICAL: Additional safety delay to ensure AudioConnectionManager
        // has FULLY completed its cleanup sequence before we proceed.
        // AudioConnectionManager.muteAndDisconnect() takes ~15ms total:
        //   - 10ms ISR safety delay
        //   - 1-3ms to delete connections
        //   - 5ms deletion propagation delay
        // SPCPlayer.stop() already has 10ms delay, so we need 10ms more here.
        delay(10);
        // // Serial.println("[SPCPlayer] Post-stop safety delay complete");
    }

    state_ = PlayerState::LOADING;

    // Free previous file data
    if (file_data_) {
        delete[] file_data_;
        file_data_ = nullptr;
        file_size_ = 0;
    }

    // Save filename
    strncpy(currentFileName_, path, sizeof(currentFileName_) - 1);
    currentFileName_[sizeof(currentFileName_) - 1] = 0;

    // Open file
    File file = fileSource_->open(path, FILE_READ);
    if (!file) {
        // // Serial.printf("ERROR: Failed to open file: %s\n", path);
        return false;
    }

    // Get file size
    file_size_ = file.size();
    // // Serial.printf("File size: %lu bytes\n", file_size_);

    // Minimum SPC file size check
    if (file_size_ < 0x10200) { // Header + RAM + DSP registers minimum
        // // Serial.println("ERROR: File too small to be a valid SPC");
        file.close();
        return false;
    }

    // Allocate memory
    file_data_ = new uint8_t[file_size_];
    if (!file_data_) {
        // // Serial.println("ERROR: Failed to allocate memory for file");
        file.close();
        return false;
    }

    // Read file
    size_t bytes_read = file.read(file_data_, file_size_);
    file.close();

    if (bytes_read != file_size_) {
        // // Serial.printf("ERROR: Read %lu bytes, expected %lu\n", bytes_read, file_size_);
        delete[] file_data_;
        file_data_ = nullptr;
        return false;
    }

    // Let the library validate the file - it knows best
    const char* error = spc_load_spc(spc_emu_, file_data_, file_size_);
    if (error) {
        // // Serial.printf("ERROR: Failed to load SPC: %s\n", error);
        delete[] file_data_;
        file_data_ = nullptr;
        return false;
    }

    // Parse header for metadata after successful load
    parseHeader(file_data_, file_size_);

    // Clear echo buffer to avoid pops
    spc_clear_echo(spc_emu_);

    // Calculate duration
    calculateDuration();

    // Reset playback state
    samples_played_ = 0;
    samples_consumed_ = 0;
    write_pos_ = 0;
    read_pos_ = 0;
    resampler_.position = 0.0f;
    resampler_.prev_l = 0;
    resampler_.prev_r = 0;

    // // Serial.println("[SPCPlayer] File loaded successfully");

    // Print metadata if available
    if (has_id666_) {
        // // Serial.printf("  Song: %s\n", id666_.song_title);
        // // Serial.printf("  Game: %s\n", id666_.game_title);
        // // Serial.printf("  Artist: %s\n", id666_.artist);
        // // Serial.printf("  Duration: %lu seconds\n", total_samples_ / (uint32_t)SPC_SAMPLE_RATE);
    }

    state_ = PlayerState::STOPPED;
    return true;
}

// Parse SPC header for metadata
bool SPCPlayer::parseHeader(const uint8_t* data, size_t size) {
    // Check signature (but don't fail - library already validated)
    if (memcmp(data, "SNES-SPC700 Sound File Data", 28) != 0) {
        // // Serial.println("Warning: Non-standard SPC signature (file still loaded)");
        has_id666_ = false;
        return true;  // Continue anyway
    }

    // Check version
    if (data[0x21] != 26 || data[0x22] != 26) {
        // // Serial.println("Warning: Non-standard SPC version bytes");
        has_id666_ = false;
        return true;  // Continue anyway
    }

    // Check for ID666 tag
    has_id666_ = (data[0x23] == 26);

    if (has_id666_) {
        // // Serial.println("ID666 tag present");
        // Determine if text or binary format
        // Binary format is indicated by specific patterns in the data
        bool is_text_format = true;

        // Simple heuristic: check if date field looks like text (MM/DD/YYYY)
        const uint8_t* date_field = &data[0x9E];
        if (date_field[2] == '/' && date_field[5] == '/') {
            is_text_format = true;
        } else {
            is_text_format = false;
        }

        parseID666Tag(&data[0x2E], is_text_format);
    }

    return true;
}

// Parse ID666 tag
bool SPCPlayer::parseID666Tag(const uint8_t* data, bool text_format) {
    memset(&id666_, 0, sizeof(id666_));

    if (text_format) {
        // Text format
        memcpy(id666_.song_title, &data[0x00], 32);
        memcpy(id666_.game_title, &data[0x20], 32);
        memcpy(id666_.dumper, &data[0x40], 16);
        memcpy(id666_.comments, &data[0x50], 32);
        memcpy(id666_.dump_date, &data[0x70], 11);

        // Parse fade times (stored as text)
        char seconds_str[4] = {0};
        memcpy(seconds_str, &data[0x7B], 3);
        id666_.seconds_before_fade = atoi(seconds_str);

        char fade_str[6] = {0};
        memcpy(fade_str, &data[0x7E], 5);
        id666_.fade_length_ms = atoi(fade_str);

        memcpy(id666_.artist, &data[0x83], 32);
        id666_.default_channel_disabled = data[0xA3];
        id666_.emulator_used = data[0xA4];
    } else {
        // Binary format
        memcpy(id666_.song_title, &data[0x00], 32);
        memcpy(id666_.game_title, &data[0x20], 32);
        memcpy(id666_.dumper, &data[0x40], 16);
        memcpy(id666_.comments, &data[0x50], 32);

        // Binary date is stored as 4 bytes (YYYYMMDD)
        uint32_t date = *(uint32_t*)&data[0x70];
        sprintf(id666_.dump_date, "%04d/%02d/%02d",
                date / 10000, (date / 100) % 100, date % 100);

        // Binary format times are 32-bit values
        id666_.seconds_before_fade = *(uint32_t*)&data[0x74];
        id666_.fade_length_ms = *(uint32_t*)&data[0x78];

        memcpy(id666_.artist, &data[0x7C], 32);
        id666_.default_channel_disabled = data[0x9C];
        id666_.emulator_used = data[0x9D];
    }

    // Null-terminate strings
    id666_.song_title[32] = 0;
    id666_.game_title[32] = 0;
    id666_.dumper[16] = 0;
    id666_.comments[32] = 0;
    id666_.artist[32] = 0;

    return true;
}

// Calculate duration based on ID666 or default
void SPCPlayer::calculateDuration() {
    if (has_id666_ && id666_.seconds_before_fade > 0) {
        // Use ID666 timing
        fade_start_sample_ = id666_.seconds_before_fade * SPC_SAMPLE_RATE;
        fade_length_samples_ = (id666_.fade_length_ms * SPC_SAMPLE_RATE) / 1000;

        if (fade_length_samples_ == 0) {
            fade_length_samples_ = 10 * SPC_SAMPLE_RATE; // Default 10 second fade
        }

        total_samples_ = fade_start_sample_ + fade_length_samples_;
    } else {
        // Default: 3 minutes play + 10 seconds fade
        fade_start_sample_ = 180 * SPC_SAMPLE_RATE;
        fade_length_samples_ = 10 * SPC_SAMPLE_RATE;
        total_samples_ = fade_start_sample_ + fade_length_samples_;
    }
}

// Play
void SPCPlayer::play() {
    Serial.println("=============================================");
    Serial.println("[SPCPlayer] play() CALLED - DETAILED DEBUG");
    Serial.println("=============================================");
    Serial.printf("[SPCPlayer] file_data_=%p, state_=%d\n", file_data_, (int)state_);
    Serial.printf("[SPCPlayer] audio_stream_=%p\n", audio_stream_);
    Serial.printf("[SPCPlayer] spc_emu_=%p\n", spc_emu_);
    Serial.printf("[SPCPlayer] Current file: %s\n", currentFileName_);

    if (!file_data_ || state_ == PlayerState::PLAYING) {
        Serial.println("[SPCPlayer] play() ABORTED - file_data is null or already playing");
        return;
    }

    Serial.println("[SPCPlayer] play() PROCEEDING - Starting playback");

    // Reset buffer positions first
    write_pos_ = 0;
    read_pos_ = 0;
    samples_played_ = 0;
    samples_consumed_ = 0;

    // Clear filter before playing (from example)
    if (filter_) {
        spc_filter_clear(filter_);
        Serial.println("[SPCPlayer] Filter cleared");
    }

    // Set state to PLAYING so fillBuffer() will work
    state_ = PlayerState::PLAYING;
    Serial.printf("[SPCPlayer] State changed to PLAYING (%d)\n", (int)state_);

    // Pre-fill the buffer before unmuting
    Serial.println("[SPCPlayer] Pre-filling buffer...");
    fillBuffer();
    Serial.printf("[SPCPlayer] Buffer pre-filled, write_pos=%d, read_pos=%d\n", write_pos_, read_pos_);

    // Unmute submixer channel 1 (connections already exist from constructor)
    if (mixerLeft_ && mixerRight_) {
        mixerLeft_->gain(1, 0.8f);   // SPC at 80% on submixer channel 1
        mixerRight_->gain(1, 0.8f);
        Serial.println("[SPCPlayer] SPC mixer channel unmuted (gain=0.8)");
    } else {
        Serial.println("[SPCPlayer] ERROR: Mixer pointers are null!");
    }

    // CRITICAL: Mute line-in (main mixer channel 0) when using SPC emulator
    // Otherwise we'll hear noise/stuck notes from OPL3/Genesis hardware
    if (mainMixerLeft_ && mainMixerRight_) {
        AudioSystem::muteLineIn(*mainMixerLeft_, *mainMixerRight_);
        Serial.println("[SPCPlayer] Line-in muted (using SPC emulator, not hardware)");
    }

    // Verify AudioStreamSPC is connected
    if (audio_stream_) {
        Serial.printf("[SPCPlayer] AudioStreamSPC is connected at 0x%08X\n", (uint32_t)audio_stream_);
    } else {
        Serial.println("[SPCPlayer] ERROR: AudioStreamSPC is null!");
    }

    Serial.println("[SPCPlayer] play() COMPLETE - Should be playing now");
    Serial.println("=============================================");
}

// Stop
void SPCPlayer::stop() {
    if (state_ == PlayerState::IDLE || state_ == PlayerState::STOPPED) return;

    // // Serial.println("[SPCPlayer] Stopping playback");
    state_ = PlayerState::STOPPING;

    // STEP 1: No timer to stop (SPC uses Audio Library update() callback)

    // STEP 2: Safety delay for Audio Library ISR to complete
    delay(10);  // 10ms ensures Audio Library ISR finishes any in-flight update()

    // NOTE: Audio routing (mute) now handled by PlayerManager
    // PlayerManager calls setFadeGain(0.0) after calling stop()

    // STEP 3: Mute submixer channel 1 (SPC audio)
    // Persistent connections stay alive, just mute them
    mixerLeft_->gain(1, 0.0f);
    mixerRight_->gain(1, 0.0f);

    // // Serial.println("[SPCPlayer] SPC mixer channel muted");

    // Clear buffers
    write_pos_ = 0;
    read_pos_ = 0;
    samples_played_ = 0;
    samples_consumed_ = 0;

    // Reset emulator for next play
    if (spc_emu_ && file_data_) {
        spc_load_spc(spc_emu_, file_data_, file_size_);
        spc_clear_echo(spc_emu_);
    }

    state_ = PlayerState::STOPPED;
    // // Serial.println("[SPCPlayer] Stop complete");
}

// Pause
void SPCPlayer::pause() {
    if (state_ != PlayerState::PLAYING) return;

    // // Serial.println("[SPCPlayer] Pausing");
    state_ = PlayerState::PAUSED;
}

// Resume
void SPCPlayer::resume() {
    if (state_ != PlayerState::PAUSED) return;

    // // Serial.println("[SPCPlayer] Resuming");
    state_ = PlayerState::PLAYING;
}

// Reset
void SPCPlayer::reset() {
    stop();

    // Reset emulator if file is loaded
    if (spc_emu_ && file_data_) {
        spc_load_spc(spc_emu_, file_data_, file_size_);
        spc_clear_echo(spc_emu_);
        samples_played_ = 0;
        samples_consumed_ = 0;
        resampler_.position = 0.0f;
        resampler_.prev_l = 0;
        resampler_.prev_r = 0;
    }
}

// Timer ISR
void SPCPlayer::fillBufferISR() {
    if (instance_) {
        instance_->fillBuffer();
    }
}

// Fill audio buffer
void SPCPlayer::fillBuffer() {
    static int fillCallCount = 0;
    fillCallCount++;

    if (fillCallCount <= 10) {
        Serial.printf("[SPCPlayer] fillBuffer() called #%d, state=%d, spc_emu=%p\n",
                     fillCallCount, (int)state_, spc_emu_);
    }

    if (state_ != PlayerState::PLAYING || !spc_emu_) {
        if (fillCallCount <= 10) {
            Serial.println("[SPCPlayer] fillBuffer() early return - not playing or no emulator");
        }
        return;
    }

    // Calculate how many stereo pairs are currently buffered
    // Ring buffer stores interleaved samples, so total samples / 2 = stereo pairs
    uint32_t samples_in_buffer = (write_pos_ - read_pos_) & ((RING_BUFFER_SIZE * 2) - 1);
    uint32_t stereo_pairs = samples_in_buffer / 2;

    // Only generate if we have room for a full buffer (1411 pairs)
    // We can hold RING_BUFFER_SIZE pairs total
    if (stereo_pairs > (RING_BUFFER_SIZE - 1500)) return;  // Buffer is full enough

    // Check if we've reached the end
    if (samples_played_ >= total_samples_) {
        stop();

        // Notify completion (natural end)
        if (completionCallback_) {
            completionCallback_();
        }
        return;
    }

    // Generate SPC samples at 32kHz
    // Buffer must hold SAMPLES_PER_BLOCK samples (already accounts for stereo)
    int16_t spc_buffer[SAMPLES_PER_BLOCK]; // 2048 samples total

    // Pass SAMPLES_PER_BLOCK directly - it's the total sample count like in example
    const char* error = spc_play(spc_emu_, SAMPLES_PER_BLOCK, spc_buffer);

    if (error) {
        // // Serial.printf("ERROR: SPC play error: %s\n", error);
        stop();
        return;
    }



    // Apply filter if enabled (for authentic SNES sound)
    extern bool g_spcFilterEnabled;

    // Debug: Log filter state occasionally
    static int fillCount = 0;
    if (++fillCount % 100 == 0) {  // Every 100 fills (about once per second)
        // Serial.printf("SPCPlayer: Filter is %s (g_spcFilterEnabled=%d)\n",
        //              g_spcFilterEnabled ? "ENABLED" : "DISABLED",
        //              g_spcFilterEnabled);
    }

    if (g_spcFilterEnabled && filter_) {
        // Filter expects same count as spc_play
        spc_filter_run(filter_, spc_buffer, SAMPLES_PER_BLOCK);
    }

    // Apply fade if needed
    if (samples_played_ >= fade_start_sample_) {
        applyFade(spc_buffer, SAMPLES_PER_BLOCK);
    }

    // Resample from 32kHz to 44.1kHz
    // We have 1024 stereo pairs (2048 samples total)
    // Output: 1024 * (44100/32000) = ~1410 stereo pairs = 2820 samples
    size_t input_pairs = SAMPLES_PER_BLOCK / 2;  // 1024 stereo pairs
    // CRITICAL: Must use floating point math to get correct ratio!
    float ratio = TEENSY_SAMPLE_RATE / SPC_SAMPLE_RATE;  // Should be 1.378125
    size_t output_pairs = (size_t)((float)input_pairs * ratio);  // Should be 1411

    // Debug: Verify the calculation
    static bool first_time = true;
    if (first_time) {
        // Serial.printf("Resampling: %d input pairs -> %d output pairs (ratio=%.6f, increment=%.6f)\n",
        //              input_pairs, output_pairs, ratio, resampler_.increment);
        first_time = false;
    }

    // Need stereo samples: 1410 pairs * 2 = 2820 samples (allocate extra)
    int16_t resampled[3000]; // Enough for output

    resampleBuffer(spc_buffer, resampled, input_pairs, output_pairs);

    // Debug: Log buffer write
    static int writeDebugCount = 0;
    size_t old_write_pos = write_pos_;

    // Write to ring buffer (output_pairs * 2 for total samples)
    for (size_t i = 0; i < output_pairs * 2; i++) {
        ring_buffer_[write_pos_] = resampled[i];
        write_pos_ = (write_pos_ + 1) & ((RING_BUFFER_SIZE * 2) - 1);
    }

    if (writeDebugCount++ < 10) {
        // Serial.printf("Wrote %d samples: write_pos %d->%d, first samples=[%d,%d,%d,%d]\n",
        //              output_pairs * 2, old_write_pos, write_pos_,
        //              resampled[0], resampled[1], resampled[2], resampled[3]);
    }

    // Track samples at 32kHz (source rate) for fade calculations
    samples_played_ += SAMPLES_PER_BLOCK;  // Track total samples generated at 32kHz
}

// Simple linear resampling
// src_pairs and dst_pairs are stereo pair counts (half the total sample count)
void SPCPlayer::resampleBuffer(const int16_t* src, int16_t* dst, size_t src_pairs, size_t dst_pairs) {
    // CRITICAL: Each buffer is independent! Don't carry position between buffers.
    // We always start from position 0 for each new buffer.
    float pos = 0.0f;
    const float increment = resampler_.increment;

    for (size_t i = 0; i < dst_pairs; i++) {
        size_t idx = (size_t)pos;
        float frac = pos - idx;

        if (idx < src_pairs - 1) {
            // Linear interpolation for stereo pairs
            int16_t curr_l = src[idx * 2];
            int16_t curr_r = src[idx * 2 + 1];
            int16_t next_l = src[(idx + 1) * 2];
            int16_t next_r = src[(idx + 1) * 2 + 1];

            dst[i * 2] = curr_l + (int16_t)((next_l - curr_l) * frac);
            dst[i * 2 + 1] = curr_r + (int16_t)((next_r - curr_r) * frac);
        } else {
            // Use last sample pair when we run out
            dst[i * 2] = src[(src_pairs - 1) * 2];
            dst[i * 2 + 1] = src[(src_pairs - 1) * 2 + 1];
        }

        pos += increment;
    }

    // Debug: Log resampler state
    static int resampleCount = 0;
    if (resampleCount++ < 20) {
        // Serial.printf("Resample #%d: final pos=%.3f (after %d->%d pairs)\n",
        //              resampleCount, pos, src_pairs, dst_pairs);
    }

    // No need to save position - each buffer is independent
}

// Apply fade out
void SPCPlayer::applyFade(int16_t* buffer, size_t samples) {
    for (size_t i = 0; i < samples; i++) {
        uint32_t fade_pos = (samples_played_ + i) - fade_start_sample_;

        if (fade_pos < fade_length_samples_) {
            float fade = 1.0f - ((float)fade_pos / fade_length_samples_);
            buffer[i * 2] = (int16_t)(buffer[i * 2] * fade);
            buffer[i * 2 + 1] = (int16_t)(buffer[i * 2 + 1] * fade);
        } else {
            buffer[i * 2] = 0;
            buffer[i * 2 + 1] = 0;
        }
    }
}

// Fill audio blocks for AudioStream
bool SPCPlayer::fillAudioBuffer(int16_t* left, int16_t* right, size_t samples) {
    // NO Serial.print here - this is called from ISR context!

    if (state_ != PlayerState::PLAYING) {
        // Output silence
        memset(left, 0, samples * sizeof(int16_t));
        memset(right, 0, samples * sizeof(int16_t));
        return false;
    }

    // DO NOT call fillBuffer() here - it's too heavy for interrupt context!
    // fillBuffer() should be called from update() in the main loop instead

    // Calculate available stereo pairs in the buffer
    size_t available_samples = (write_pos_ - read_pos_) & ((RING_BUFFER_SIZE * 2) - 1);
    size_t available = available_samples / 2;  // Convert to stereo pairs

    // NO debug output in ISR context

    if (available < samples) {
        // Not enough data, output what we have and pad with silence
        for (size_t i = 0; i < samples; i++) {
            if (i < available) {
                left[i] = ring_buffer_[read_pos_];
                read_pos_ = (read_pos_ + 1) & ((RING_BUFFER_SIZE * 2) - 1);
                right[i] = ring_buffer_[read_pos_];
                read_pos_ = (read_pos_ + 1) & ((RING_BUFFER_SIZE * 2) - 1);
            } else {
                left[i] = 0;
                right[i] = 0;
            }
        }
        return false;
    }

    // Read from ring buffer
    for (size_t i = 0; i < samples; i++) {
        left[i] = ring_buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) & ((RING_BUFFER_SIZE * 2) - 1);
        right[i] = ring_buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) & ((RING_BUFFER_SIZE * 2) - 1);
    }

    // Track samples actually consumed at 44.1kHz rate
    samples_consumed_ += samples;

    return true;
}

// Update (called from main loop)
void SPCPlayer::update() {
    // Update playback state if needed
    if (state_ == PlayerState::PLAYING) {
        // Debug: See how often update is called
        static unsigned long lastUpdate = 0;
        static int updateCount = 0;
        unsigned long now = millis();
        if (updateCount++ < 20) {
            Serial.printf("SPCPlayer::update() #%d called, dt=%lums\n",
                         updateCount, now - lastUpdate);

            // Check if AudioStreamSPC is getting updates
            if (audio_stream_) {
                Serial.printf("  AudioStreamSPC update count: %lu\n",
                             audio_stream_->getUpdateCount());
            }
        }
        lastUpdate = now;

        // CRITICAL: Only fill buffer when there's space for more data
        // The ring buffer is RING_BUFFER_SIZE * 2 total samples (interleaved stereo)
        // Calculate how many stereo samples are in the buffer
        size_t samples_in_buffer = (write_pos_ - read_pos_) & ((RING_BUFFER_SIZE * 2) - 1);
        size_t stereo_pairs_in_buffer = samples_in_buffer / 2;

        // We can hold RING_BUFFER_SIZE stereo pairs total
        size_t space_for_pairs = RING_BUFFER_SIZE - stereo_pairs_in_buffer;

        // Only fill if we have room for at least one buffer's worth (about 1411 pairs)
        if (space_for_pairs >= 1500) {  // A bit more than 1411 for safety
            // Debug: Log when we actually fill
            static int fillCount = 0;
            static unsigned long lastFillTime = 0;
            unsigned long fillTime = millis();

            size_t before_pairs = stereo_pairs_in_buffer;

            fillBuffer();

            // Recalculate after filling
            size_t new_samples = (write_pos_ - read_pos_) & ((RING_BUFFER_SIZE * 2) - 1);
            size_t after_pairs = new_samples / 2;

            if (fillCount++ < 30) {
                // Serial.printf("Fill #%d: dt=%lums, had %d, now %d pairs (added %d)\n",
                //              fillCount, fillTime - lastFillTime, before_pairs, after_pairs,
                //              after_pairs - before_pairs);
            }
            lastFillTime = fillTime;
        }

        // Check for end of playback using 32kHz samples
        // Compare samples_played_ (32kHz) with total_samples_ (32kHz)
        if (samples_played_ >= total_samples_) {
            stop();

            // Notify completion (natural end)
            if (completionCallback_) {
                completionCallback_();
            }
        }
    }
}

// Position tracking
uint32_t SPCPlayer::getPositionMs() const {
    if (state_ == PlayerState::IDLE) return 0;
    // Use samples_consumed_ which tracks at 44.1kHz output rate, not samples_played_ at 32kHz
    // Convert from 44.1kHz samples to milliseconds
    return (samples_consumed_ * 1000) / (uint32_t)TEENSY_SAMPLE_RATE;
}

uint32_t SPCPlayer::getDurationMs() const {
    return (total_samples_ * 1000) / (uint32_t)SPC_SAMPLE_RATE;
}

float SPCPlayer::getProgress() const {
    if (total_samples_ == 0) return 0.0f;
    return (float)samples_played_ / (float)total_samples_;
}

// Mute voice
void SPCPlayer::muteVoice(int voice, bool mute) {
    if (!spc_emu_ || voice < 0 || voice >= 8) return;

    static int mute_mask = 0;

    if (mute) {
        mute_mask |= (1 << voice);
    } else {
        mute_mask &= ~(1 << voice);
    }

    spc_mute_voices(spc_emu_, mute_mask);
}

// Set tempo
void SPCPlayer::setTempo(float tempo) {
    if (!spc_emu_) return;

    // Convert float to fixed point (0x100 = 1.0)
    int tempo_fixed = (int)(tempo * 0x100);
    spc_set_tempo(spc_emu_, tempo_fixed);
}

// AudioStreamSPC implementation moved to audio_stream_spc.cpp