/**
 * @file audio_stream_fm9_wav.cpp
 * @brief Implementation of AudioStreamFM9Wav
 */

#include "audio_stream_fm9_wav.h"
#include <string.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

AudioStreamFM9Wav::AudioStreamFM9Wav()
    : AudioStream(0, nullptr)  // No inputs, 2 outputs
    , fileLoaded_(false)
    , totalSamples_(0)
    , currentSample_(0)
    , baseOffset_(0)
    , dataStartOffset_(0)
    , sampleRate_(44100)
    , numChannels_(2)
    , bitsPerSample_(16)
    , bytesPerSample_(4)
    , playing_(false)
    , paused_(false)
    , readBufferLeft_(nullptr)
    , readBufferRight_(nullptr)
    , bufferReadPos_(0)
    , bufferWritePos_(0)
    , bufferAvailable_(0)
    , fileReadSample_(0)
    , endOfFile_(false)
    , targetSample_(0)
    , seekRequested_(false)
    , seekTargetSample_(0)
    , lastTargetSample_(0)
    , syncMode_(0)
    , syncEnabled_(false)
    , underruns_(0)
    , seekCount_(0)
    , speedupCount_(0)
    , slowdownCount_(0) {

    Serial.println("[AudioStreamFM9Wav] Constructor - registering with Audio Library");

    // Allocate buffers in PSRAM for large capacity
    readBufferLeft_ = (int16_t*)extmem_malloc(BUFFER_SAMPLES * sizeof(int16_t));
    readBufferRight_ = (int16_t*)extmem_malloc(BUFFER_SAMPLES * sizeof(int16_t));

    if (!readBufferLeft_ || !readBufferRight_) {
        Serial.println("[AudioStreamFM9Wav] ERROR: Failed to allocate PSRAM buffers!");
        if (readBufferLeft_) { extmem_free(readBufferLeft_); readBufferLeft_ = nullptr; }
        if (readBufferRight_) { extmem_free(readBufferRight_); readBufferRight_ = nullptr; }
    } else {
        Serial.printf("[AudioStreamFM9Wav] Allocated %d samples (%.1f ms) in PSRAM\n",
                      BUFFER_SAMPLES, BUFFER_SAMPLES / 44.1f);
    }
}

AudioStreamFM9Wav::~AudioStreamFM9Wav() {
    closeFile();
    if (readBufferLeft_) { extmem_free(readBufferLeft_); readBufferLeft_ = nullptr; }
    if (readBufferRight_) { extmem_free(readBufferRight_); readBufferRight_ = nullptr; }
}

// ============================================================================
// File Management
// ============================================================================

bool AudioStreamFM9Wav::loadFile(const char* path) {
    closeFile();

    if (!readBufferLeft_ || !readBufferRight_) {
        Serial.println("[AudioStreamFM9Wav] ERROR: No PSRAM buffers!");
        return false;
    }

    Serial.print("[AudioStreamFM9Wav] Loading: ");
    Serial.println(path);

    file_ = SD.open(path, FILE_READ);
    if (!file_) {
        Serial.println("[AudioStreamFM9Wav] ERROR: Failed to open file");
        return false;
    }

    // Standalone WAV file - no base offset
    baseOffset_ = 0;

    if (!parseWavHeader()) {
        Serial.println("[AudioStreamFM9Wav] ERROR: Invalid WAV header");
        file_.close();
        return false;
    }

    fileLoaded_ = true;
    Serial.printf("[AudioStreamFM9Wav] Loaded: %d samples, %d Hz, %d ch, %d bit\n",
                  totalSamples_, sampleRate_, numChannels_, bitsPerSample_);

    return true;
}

bool AudioStreamFM9Wav::loadFromOffset(const char* path, uint32_t audioOffset, uint32_t audioSize) {
    closeFile();

    if (!readBufferLeft_ || !readBufferRight_) {
        Serial.println("[AudioStreamFM9Wav] ERROR: No PSRAM buffers!");
        return false;
    }

    Serial.print("[AudioStreamFM9Wav] Loading from offset ");
    Serial.print(audioOffset);
    Serial.print(" in: ");
    Serial.println(path);

    file_ = SD.open(path, FILE_READ);
    if (!file_) {
        Serial.println("[AudioStreamFM9Wav] ERROR: Failed to open file");
        return false;
    }

    // Set base offset - WAV header parsing will be relative to this
    baseOffset_ = audioOffset;

    // Seek to start of WAV data within the file
    file_.seek(baseOffset_);

    if (!parseWavHeader()) {
        Serial.println("[AudioStreamFM9Wav] ERROR: Invalid WAV header at offset");
        file_.close();
        return false;
    }

    fileLoaded_ = true;
    Serial.printf("[AudioStreamFM9Wav] Loaded from offset: %d samples, %d Hz, %d ch, %d bit\n",
                  totalSamples_, sampleRate_, numChannels_, bitsPerSample_);

    return true;
}

void AudioStreamFM9Wav::closeFile() {
    stop();
    if (file_) {
        file_.close();
    }
    fileLoaded_ = false;
    totalSamples_ = 0;
    currentSample_ = 0;
}

bool AudioStreamFM9Wav::parseWavHeader() {
    uint8_t header[44];

    // Read header (file should already be at baseOffset_)
    if (file_.read(header, 44) != 44) {
        return false;
    }

    // Check RIFF header
    if (memcmp(header, "RIFF", 4) != 0) {
        Serial.println("[AudioStreamFM9Wav] Not RIFF");
        return false;
    }

    // Check WAVE format
    if (memcmp(header + 8, "WAVE", 4) != 0) {
        Serial.println("[AudioStreamFM9Wav] Not WAVE");
        return false;
    }

    // Check fmt chunk
    if (memcmp(header + 12, "fmt ", 4) != 0) {
        Serial.println("[AudioStreamFM9Wav] No fmt chunk");
        return false;
    }

    // Parse format
    uint16_t audioFormat = header[20] | (header[21] << 8);
    numChannels_ = header[22] | (header[23] << 8);
    sampleRate_ = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    bitsPerSample_ = header[34] | (header[35] << 8);

    if (audioFormat != 1) {  // PCM
        Serial.println("[AudioStreamFM9Wav] Not PCM format");
        return false;
    }

    if (bitsPerSample_ != 16) {
        Serial.printf("[AudioStreamFM9Wav] Unsupported bits: %d\n", bitsPerSample_);
        return false;
    }

    bytesPerSample_ = (bitsPerSample_ / 8) * numChannels_;

    // Find data chunk (may not be at offset 36)
    // Seek relative to baseOffset_ (where WAV starts in the file)
    file_.seek(baseOffset_ + 12);  // After RIFF header
    while (file_.available()) {
        char chunkId[4];
        uint32_t chunkSize;

        if (file_.read(chunkId, 4) != 4) break;
        if (file_.read(&chunkSize, 4) != 4) break;

        if (memcmp(chunkId, "data", 4) == 0) {
            // Store absolute file position where PCM data starts
            dataStartOffset_ = file_.position();
            totalSamples_ = chunkSize / bytesPerSample_;
            Serial.printf("[AudioStreamFM9Wav] Data chunk at %d, %d bytes\n",
                          dataStartOffset_, chunkSize);
            return true;
        }

        // Skip this chunk
        file_.seek(file_.position() + chunkSize);
    }

    Serial.println("[AudioStreamFM9Wav] No data chunk found");
    return false;
}

// ============================================================================
// Playback Control
// ============================================================================

void AudioStreamFM9Wav::play() {
    if (!fileLoaded_) return;

    Serial.println("[AudioStreamFM9Wav] play()");

    // Reset state
    currentSample_ = 0;
    fileReadSample_ = 0;
    targetSample_ = 0;
    lastTargetSample_ = 0;
    seekRequested_ = false;
    seekTargetSample_ = 0;
    syncMode_ = 0;
    syncEnabled_ = false;  // Sync disabled until first setTargetSample() call
    endOfFile_ = false;
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;
    underruns_ = 0;
    seekCount_ = 0;
    speedupCount_ = 0;
    slowdownCount_ = 0;

    // Seek to data start
    file_.seek(dataStartOffset_);

    // Pre-fill buffer
    refillBuffer();
    refillBuffer();  // Fill as much as possible

    playing_ = true;
    paused_ = false;

    Serial.printf("[AudioStreamFM9Wav] Started with %d samples buffered\n", bufferAvailable_);
}

void AudioStreamFM9Wav::stop() {
    playing_ = false;
    paused_ = false;
}

void AudioStreamFM9Wav::pause() {
    paused_ = true;
}

void AudioStreamFM9Wav::resume() {
    paused_ = false;
}

float AudioStreamFM9Wav::getProgress() const {
    if (totalSamples_ == 0) return 0.0f;
    return (float)currentSample_ / (float)totalSamples_;
}

// ============================================================================
// Synchronization
// ============================================================================

void AudioStreamFM9Wav::setTargetSample(uint32_t targetSample) {
    // Enable sync on first call - this is when VGM playback has actually started
    if (!syncEnabled_) {
        syncEnabled_ = true;
        // Align currentSample_ with target to start in sync
        // (WAV may have advanced a few samples during pre-fill, reset to match VGM)
        currentSample_ = targetSample;
        lastTargetSample_ = targetSample;
        targetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Wav] Sync enabled, aligned to sample %u\n", targetSample);
        return;
    }

    // Detect backward jump (loop) - if target suddenly drops significantly
    // A big backward jump (>1000 samples back) indicates VGM looped
    if (targetSample + 1000 < lastTargetSample_ && !seekRequested_) {
        // VGM looped back - request seek to match
        seekRequested_ = true;
        seekTargetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Wav] Loop detected! target=%u, last=%u, requesting seek\n",
                      targetSample, lastTargetSample_);
    }

    // Check for extreme drift that requires a seek (>100ms)
    int32_t drift = (int32_t)currentSample_ - (int32_t)targetSample;
    if ((drift > SYNC_MAX_DRIFT || drift < -SYNC_MAX_DRIFT) && !seekRequested_) {
        seekRequested_ = true;
        seekTargetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Wav] Extreme drift %d samples, requesting seek\n", drift);
    }

    lastTargetSample_ = targetSample;
    targetSample_ = targetSample;
}

int32_t AudioStreamFM9Wav::getSyncDrift() const {
    return (int32_t)currentSample_ - (int32_t)targetSample_;
}

// ============================================================================
// Buffer Management
// ============================================================================

size_t AudioStreamFM9Wav::available() const {
    return bufferAvailable_;
}

size_t AudioStreamFM9Wav::space() const {
    return BUFFER_SAMPLES - bufferAvailable_;
}

bool AudioStreamFM9Wav::needsRefill() const {
    return fileLoaded_ && !endOfFile_ && (bufferAvailable_ < REFILL_THRESHOLD);
}

size_t AudioStreamFM9Wav::getBufferLevel() const {
    return bufferAvailable_;
}

void AudioStreamFM9Wav::refillBuffer() {
    if (!fileLoaded_ || !file_) return;
    if (!readBufferLeft_ || !readBufferRight_) return;

    // Handle seek request from loop detection (must be done in main loop, not ISR)
    if (seekRequested_) {
        uint32_t targetSample = seekTargetSample_;

        // Calculate file position for target sample
        uint32_t filePos = dataStartOffset_ + (targetSample * bytesPerSample_);

        Serial.printf("[AudioStreamFM9Wav] Seeking to sample %u (file pos %u)\n",
                      targetSample, filePos);

        // Seek file
        file_.seek(filePos);

        // Reset buffer state
        __disable_irq();
        bufferReadPos_ = 0;
        bufferWritePos_ = 0;
        bufferAvailable_ = 0;
        currentSample_ = targetSample;
        fileReadSample_ = targetSample;
        endOfFile_ = false;
        seekRequested_ = false;
        __enable_irq();

        seekCount_++;

        // Pre-fill buffer after seek
        // (will continue below)
    }

    if (endOfFile_) return;

    size_t freeSpace = space();
    if (freeSpace < 128) return;  // Not worth the SD access

    // Limit read size to avoid blocking too long
    size_t samplesToRead = min(freeSpace, (size_t)512);

    // Temporary buffer for interleaved read
    static int16_t tempBuf[1024];  // 512 stereo samples max
    size_t bytesToRead = samplesToRead * bytesPerSample_;
    if (bytesToRead > sizeof(tempBuf)) {
        bytesToRead = sizeof(tempBuf);
        samplesToRead = bytesToRead / bytesPerSample_;
    }

    size_t bytesRead = file_.read(tempBuf, bytesToRead);
    size_t samplesRead = bytesRead / bytesPerSample_;

    if (samplesRead == 0) {
        endOfFile_ = true;
        return;
    }

    // De-interleave into ring buffers
    __disable_irq();
    for (size_t i = 0; i < samplesRead; i++) {
        size_t writePos = bufferWritePos_;

        if (numChannels_ == 2) {
            readBufferLeft_[writePos] = tempBuf[i * 2];
            readBufferRight_[writePos] = tempBuf[i * 2 + 1];
        } else {
            // Mono: duplicate to both channels
            readBufferLeft_[writePos] = tempBuf[i];
            readBufferRight_[writePos] = tempBuf[i];
        }

        bufferWritePos_ = (writePos + 1) % BUFFER_SAMPLES;
        bufferAvailable_++;
    }
    __enable_irq();

    fileReadSample_ += samplesRead;
}

// ============================================================================
// Audio Library ISR
// ============================================================================

void AudioStreamFM9Wav::update() {
    if (!playing_ || paused_ || !readBufferLeft_ || !readBufferRight_) {
        return;
    }

    audio_block_t *left = allocate();
    audio_block_t *right = allocate();

    if (!left || !right) {
        if (left) release(left);
        if (right) release(right);
        return;
    }

    // If a seek is pending, output silence until main loop completes the seek
    if (seekRequested_) {
        memset(left->data, 0, sizeof(left->data));
        memset(right->data, 0, sizeof(right->data));
        transmit(left, 0);
        transmit(right, 1);
        release(left);
        release(right);
        return;
    }

    // Check for buffer underrun
    if (bufferAvailable_ < AUDIO_BLOCK_SAMPLES + 1) {  // +1 for potential speedup
        underruns_++;
        // Output silence
        memset(left->data, 0, sizeof(left->data));
        memset(right->data, 0, sizeof(right->data));
        transmit(left, 0);
        transmit(right, 1);
        release(left);
        release(right);
        return;
    }

    // === GRADUAL RATE ADJUSTMENT SYNC ===
    // Instead of skipping/repeating samples (causes clicks), we adjust playback rate:
    // - WAV behind target: consume 129 input samples -> 128 output (speed up ~0.78%)
    // - WAV ahead of target: consume 127 input samples -> 128 output (slow down ~0.78%)
    // - Within dead zone: consume 128 samples normally
    //
    // Linear interpolation ensures smooth audio with no discontinuities.

    // Only do sync adjustments if sync is enabled (first setTargetSample() received)
    int8_t newSyncMode = 0;
    if (syncEnabled_) {
        int32_t drift = (int32_t)currentSample_ - (int32_t)targetSample_;

        // Determine sync mode based on drift
        if (drift < -SYNC_DEAD_ZONE) {
            // WAV is behind - need to speed up (consume more samples)
            newSyncMode = 1;
        } else if (drift > SYNC_DEAD_ZONE) {
            // WAV is ahead - need to slow down (consume fewer samples)
            newSyncMode = -1;
        }

        // Track mode changes for diagnostics
        if (newSyncMode == 1 && syncMode_ != 1) speedupCount_++;
        if (newSyncMode == -1 && syncMode_ != -1) slowdownCount_++;
    }
    syncMode_ = newSyncMode;

    // Determine how many input samples we'll consume for 128 output samples
    // Normal: 128 in -> 128 out
    // Speed up: 129 in -> 128 out (linear interpolation stretches slightly)
    // Slow down: 127 in -> 128 out (linear interpolation compresses slightly)
    int inputSamples = AUDIO_BLOCK_SAMPLES + syncMode_;  // 127, 128, or 129

    // Make sure we have enough samples
    if ((size_t)inputSamples > bufferAvailable_) {
        inputSamples = AUDIO_BLOCK_SAMPLES;  // Fall back to normal if not enough
        syncMode_ = 0;
    }

    // Generate output using linear interpolation
    // For each output sample i (0-127), calculate the corresponding input position
    // Input position = i * (inputSamples - 1) / (AUDIO_BLOCK_SAMPLES - 1)
    //
    // Example for speedup (129 -> 128):
    //   Output 0 -> Input 0.0
    //   Output 127 -> Input 128.0
    //   So we read all 129 samples, smoothly interpolated
    //
    // Example for slowdown (127 -> 128):
    //   Output 0 -> Input 0.0
    //   Output 127 -> Input 126.0
    //   So we read only 127 samples, stretched to fill 128

    size_t startReadPos = bufferReadPos_;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        // Calculate fractional input position (fixed point: 16.16)
        // pos = i * (inputSamples - 1) / (AUDIO_BLOCK_SAMPLES - 1)
        uint32_t pos_fixed = ((uint32_t)i * ((inputSamples - 1) << 16)) / (AUDIO_BLOCK_SAMPLES - 1);
        uint32_t idx = pos_fixed >> 16;           // Integer part
        uint32_t frac = pos_fixed & 0xFFFF;       // Fractional part (0-65535)

        // Get the two samples to interpolate between
        size_t pos0 = (startReadPos + idx) % BUFFER_SAMPLES;
        size_t pos1 = (startReadPos + idx + 1) % BUFFER_SAMPLES;

        int16_t left0 = readBufferLeft_[pos0];
        int16_t left1 = readBufferLeft_[pos1];
        int16_t right0 = readBufferRight_[pos0];
        int16_t right1 = readBufferRight_[pos1];

        // Linear interpolation: out = s0 + (s1 - s0) * frac
        // Using fixed point: out = s0 + ((s1 - s0) * frac) >> 16
        left->data[i] = left0 + (((int32_t)(left1 - left0) * (int32_t)frac) >> 16);
        right->data[i] = right0 + (((int32_t)(right1 - right0) * (int32_t)frac) >> 16);
    }

    // Advance buffer position by the number of input samples consumed
    bufferReadPos_ = (startReadPos + inputSamples) % BUFFER_SAMPLES;
    bufferAvailable_ -= inputSamples;
    currentSample_ += inputSamples;

    // Check for end of file
    if (endOfFile_ && bufferAvailable_ == 0) {
        playing_ = false;
    }

    transmit(left, 0);
    transmit(right, 1);
    release(left);
    release(right);
}
