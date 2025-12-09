/**
 * @file audio_stream_fm9_mp3.cpp
 * @brief Implementation of AudioStreamFM9Mp3
 */

#include "audio_stream_fm9_mp3.h"
#include <string.h>

// Include Helix MP3 decoder
extern "C" {
#include "libhelix-mp3/mp3dec.h"
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

AudioStreamFM9Mp3::AudioStreamFM9Mp3()
    : AudioStream(0, nullptr)  // No inputs, 2 outputs
    , fileLoaded_(false)
    , baseOffset_(0)
    , mp3Size_(0)
    , fileReadPos_(0)
    , totalSamples_(0)
    , currentSample_(0)
    , decoder_(nullptr)
    , frameInfo_(nullptr)
    , frameBuffer_(nullptr)
    , frameBufferFill_(0)
    , frameBufferReadPos_(0)
    , decodedBufferLeft_(nullptr)
    , decodedBufferRight_(nullptr)
    , bufferReadPos_(0)
    , bufferWritePos_(0)
    , bufferAvailable_(0)
    , playing_(false)
    , paused_(false)
    , endOfFile_(false)
    , targetSample_(0)
    , seekRequested_(false)
    , seekTargetSample_(0)
    , lastTargetSample_(0)
    , syncMode_(0)
    , syncEnabled_(false)
    , underruns_(0)
    , decodeErrors_(0)
    , seekCount_(0)
    , speedupCount_(0)
    , slowdownCount_(0)
    , totalDecodedSamples_(0) {

    Serial.println("[AudioStreamFM9Mp3] Constructor - registering with Audio Library");

    // Allocate frame info structure
    frameInfo_ = new MP3FrameInfo();
    if (!frameInfo_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to allocate MP3FrameInfo!");
    }

    // Allocate frame buffer for compressed MP3 data
    frameBuffer_ = (uint8_t*)malloc(FRAME_BUFFER_SIZE);
    if (!frameBuffer_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to allocate frame buffer!");
    }

    // Allocate decoded PCM buffers in PSRAM for large capacity
    decodedBufferLeft_ = (int16_t*)extmem_malloc(BUFFER_SAMPLES * sizeof(int16_t));
    decodedBufferRight_ = (int16_t*)extmem_malloc(BUFFER_SAMPLES * sizeof(int16_t));

    if (!decodedBufferLeft_ || !decodedBufferRight_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to allocate PSRAM buffers!");
        if (decodedBufferLeft_) { extmem_free(decodedBufferLeft_); decodedBufferLeft_ = nullptr; }
        if (decodedBufferRight_) { extmem_free(decodedBufferRight_); decodedBufferRight_ = nullptr; }
    } else {
        Serial.printf("[AudioStreamFM9Mp3] Allocated %d samples (%.1f ms) in PSRAM\n",
                      BUFFER_SAMPLES, BUFFER_SAMPLES / 44.1f);
    }
}

AudioStreamFM9Mp3::~AudioStreamFM9Mp3() {
    closeFile();

    if (frameInfo_) { delete frameInfo_; frameInfo_ = nullptr; }
    if (frameBuffer_) { free(frameBuffer_); frameBuffer_ = nullptr; }
    if (decodedBufferLeft_) { extmem_free(decodedBufferLeft_); decodedBufferLeft_ = nullptr; }
    if (decodedBufferRight_) { extmem_free(decodedBufferRight_); decodedBufferRight_ = nullptr; }
}

// ============================================================================
// Decoder Management
// ============================================================================

bool AudioStreamFM9Mp3::initDecoder() {
    if (decoder_) {
        // Already initialized
        return true;
    }

    decoder_ = MP3InitDecoder();
    if (!decoder_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: MP3InitDecoder failed!");
        return false;
    }

    Serial.println("[AudioStreamFM9Mp3] Helix MP3 decoder initialized");
    return true;
}

void AudioStreamFM9Mp3::freeDecoder() {
    if (decoder_) {
        MP3FreeDecoder(decoder_);
        decoder_ = nullptr;
        Serial.println("[AudioStreamFM9Mp3] Decoder freed");
    }
}

// ============================================================================
// File Management
// ============================================================================

bool AudioStreamFM9Mp3::loadFromOffset(const char* path, uint32_t mp3Offset, uint32_t mp3Size) {
    closeFile();

    if (!frameBuffer_ || !decodedBufferLeft_ || !decodedBufferRight_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Buffers not allocated!");
        return false;
    }

    Serial.print("[AudioStreamFM9Mp3] Loading from offset ");
    Serial.print(mp3Offset);
    Serial.print(", size ");
    Serial.print(mp3Size);
    Serial.print(" in: ");
    Serial.println(path);

    file_ = SD.open(path, FILE_READ);
    if (!file_) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to open file");
        return false;
    }

    baseOffset_ = mp3Offset;
    mp3Size_ = mp3Size;
    fileReadPos_ = 0;

    // Seek to start of MP3 data
    file_.seek(baseOffset_);

    // Initialize decoder
    if (!initDecoder()) {
        file_.close();
        return false;
    }

    // Read first frame to get format info
    if (!fillFrameBuffer()) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to read initial data");
        freeDecoder();
        file_.close();
        return false;
    }

    // Find first sync word
    int syncOffset = MP3FindSyncWord(frameBuffer_, frameBufferFill_);
    if (syncOffset < 0) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: No MP3 sync word found");
        freeDecoder();
        file_.close();
        return false;
    }

    // Shift buffer to align with sync word
    if (syncOffset > 0) {
        memmove(frameBuffer_, frameBuffer_ + syncOffset, frameBufferFill_ - syncOffset);
        frameBufferFill_ -= syncOffset;
    }

    // Get frame info without decoding
    int err = MP3GetNextFrameInfo(decoder_, frameInfo_, frameBuffer_);
    if (err != 0) {
        Serial.println("[AudioStreamFM9Mp3] ERROR: Failed to get frame info");
        freeDecoder();
        file_.close();
        return false;
    }

    // Estimate total samples from file size and bitrate
    // For 128kbps stereo: ~16000 bytes per second, ~44100 samples per second
    // bytes_per_sample = bitrate / 8 / samplerate
    if (frameInfo_->bitrate > 0 && frameInfo_->samprate > 0) {
        float bytesPerSecond = frameInfo_->bitrate * 1000.0f / 8.0f;
        float durationSeconds = mp3Size_ / bytesPerSecond;
        totalSamples_ = (uint32_t)(durationSeconds * frameInfo_->samprate);
    } else {
        // Fallback estimate assuming 192kbps, 44.1kHz
        totalSamples_ = (mp3Size_ * 44100) / 24000;
    }

    fileLoaded_ = true;
    Serial.printf("[AudioStreamFM9Mp3] Loaded: ~%d samples, %d Hz, %d ch, %d kbps\n",
                  totalSamples_, frameInfo_->samprate, frameInfo_->nChans, frameInfo_->bitrate);

    return true;
}

void AudioStreamFM9Mp3::closeFile() {
    stop();
    freeDecoder();
    if (file_) {
        file_.close();
    }
    fileLoaded_ = false;
    totalSamples_ = 0;
    currentSample_ = 0;
    frameBufferFill_ = 0;
    frameBufferReadPos_ = 0;
}

float AudioStreamFM9Mp3::getProgress() const {
    if (totalSamples_ == 0) return 0.0f;
    return (float)currentSample_ / (float)totalSamples_;
}

// ============================================================================
// Playback Control
// ============================================================================

void AudioStreamFM9Mp3::play() {
    if (!fileLoaded_ || !decoder_) return;

    Serial.println("[AudioStreamFM9Mp3] play()");

    // Reset state
    currentSample_ = 0;
    totalDecodedSamples_ = 0;
    targetSample_ = 0;
    lastTargetSample_ = 0;
    seekRequested_ = false;
    seekTargetSample_ = 0;
    syncMode_ = 0;
    syncEnabled_ = false;
    endOfFile_ = false;
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;
    underruns_ = 0;
    decodeErrors_ = 0;
    seekCount_ = 0;
    speedupCount_ = 0;
    slowdownCount_ = 0;

    // Reset file position to start of MP3 data
    file_.seek(baseOffset_);
    fileReadPos_ = 0;
    frameBufferFill_ = 0;
    frameBufferReadPos_ = 0;

    // Pre-fill frame buffer
    fillFrameBuffer();

    // Pre-fill decoded buffer
    while (bufferAvailable_ < BUFFER_SAMPLES / 2 && !endOfFile_) {
        if (!decodeNextFrame()) break;
    }

    playing_ = true;
    paused_ = false;

    Serial.printf("[AudioStreamFM9Mp3] Started with %d samples buffered\n", bufferAvailable_);
}

void AudioStreamFM9Mp3::stop() {
    playing_ = false;
    paused_ = false;
}

void AudioStreamFM9Mp3::pause() {
    paused_ = true;
}

void AudioStreamFM9Mp3::resume() {
    paused_ = false;
}

// ============================================================================
// Synchronization
// ============================================================================

void AudioStreamFM9Mp3::setTargetSample(uint32_t targetSample) {
    // Enable sync on first call
    if (!syncEnabled_) {
        syncEnabled_ = true;
        currentSample_ = targetSample;
        lastTargetSample_ = targetSample;
        targetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Mp3] Sync enabled, aligned to sample %u, bufferAvail=%d\n",
                      targetSample, bufferAvailable_);
        return;
    }

    // Debug: print sync state every ~1 second (44100 samples)
    static uint32_t lastDebugTarget = 0;
    if (targetSample - lastDebugTarget > 44100) {
        int32_t drift = (int32_t)currentSample_ - (int32_t)targetSample;
        Serial.printf("[MP3 SYNC] target=%u current=%u drift=%d mode=%d\n",
                      targetSample, currentSample_, drift, syncMode_);
        lastDebugTarget = targetSample;
    }

    // Detect backward jump (loop) - if target suddenly drops significantly
    if (targetSample + 1000 < lastTargetSample_ && !seekRequested_) {
        seekRequested_ = true;
        seekTargetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Mp3] Loop detected! target=%u, last=%u, requesting seek\n",
                      targetSample, lastTargetSample_);
    }

    // Check for extreme drift that requires a seek
    int32_t drift = (int32_t)currentSample_ - (int32_t)targetSample;
    if ((drift > SYNC_MAX_DRIFT || drift < -SYNC_MAX_DRIFT) && !seekRequested_) {
        seekRequested_ = true;
        seekTargetSample_ = targetSample;
        Serial.printf("[AudioStreamFM9Mp3] Extreme drift %d samples, requesting seek\n", drift);
    }

    lastTargetSample_ = targetSample;
    targetSample_ = targetSample;
}

int32_t AudioStreamFM9Mp3::getSyncDrift() const {
    return (int32_t)currentSample_ - (int32_t)targetSample_;
}

// ============================================================================
// Buffer Management
// ============================================================================

size_t AudioStreamFM9Mp3::available() const {
    return bufferAvailable_;
}

size_t AudioStreamFM9Mp3::space() const {
    return BUFFER_SAMPLES - bufferAvailable_;
}

bool AudioStreamFM9Mp3::needsRefill() const {
    return fileLoaded_ && !endOfFile_ && (bufferAvailable_ < REFILL_THRESHOLD);
}

size_t AudioStreamFM9Mp3::getBufferLevel() const {
    return bufferAvailable_;
}

bool AudioStreamFM9Mp3::fillFrameBuffer() {
    if (!file_ || endOfFile_) return false;

    // Shift any remaining data to start of buffer
    if (frameBufferReadPos_ > 0 && frameBufferFill_ > frameBufferReadPos_) {
        size_t remaining = frameBufferFill_ - frameBufferReadPos_;
        memmove(frameBuffer_, frameBuffer_ + frameBufferReadPos_, remaining);
        frameBufferFill_ = remaining;
        frameBufferReadPos_ = 0;
    } else if (frameBufferReadPos_ > 0) {
        frameBufferFill_ = 0;
        frameBufferReadPos_ = 0;
    }

    // Calculate how much more we can read
    size_t spaceInBuffer = FRAME_BUFFER_SIZE - frameBufferFill_;
    if (spaceInBuffer < 512) return true;  // Buffer is full enough

    // Calculate remaining bytes in MP3 stream
    size_t remaining = mp3Size_ - fileReadPos_;
    size_t toRead = min(spaceInBuffer, remaining);

    if (toRead == 0) {
        endOfFile_ = true;
        return false;
    }

    size_t bytesRead = file_.read(frameBuffer_ + frameBufferFill_, toRead);
    if (bytesRead == 0) {
        endOfFile_ = true;
        return false;
    }

    frameBufferFill_ += bytesRead;
    fileReadPos_ += bytesRead;

    return true;
}

bool AudioStreamFM9Mp3::decodeNextFrame() {
    if (!decoder_ || !frameBuffer_ || !decodedBufferLeft_ || !decodedBufferRight_) {
        return false;
    }

    // Ensure we have enough compressed data
    if (frameBufferFill_ - frameBufferReadPos_ < 512) {
        if (!fillFrameBuffer()) {
            if (endOfFile_ && frameBufferFill_ - frameBufferReadPos_ == 0) {
                return false;  // Truly at end
            }
        }
    }

    size_t dataAvailable = frameBufferFill_ - frameBufferReadPos_;
    if (dataAvailable == 0) {
        return false;
    }

    // Find sync word
    int syncOffset = MP3FindSyncWord(frameBuffer_ + frameBufferReadPos_, dataAvailable);
    if (syncOffset < 0) {
        // No sync found - need more data or corrupted
        if (endOfFile_) {
            return false;
        }
        // Discard buffer and refill
        frameBufferFill_ = 0;
        frameBufferReadPos_ = 0;
        fillFrameBuffer();
        return true;  // Try again next call
    }

    // Advance read position to sync word
    frameBufferReadPos_ += syncOffset;
    dataAvailable -= syncOffset;

    // Check if we have room in decoded buffer for a full frame
    // MP3 frame is 1152 samples per channel (not total)
    if (space() < 1152) {
        return false;  // Buffer full, try again later
    }

    // Prepare pointers for decoder
    unsigned char* inPtr = frameBuffer_ + frameBufferReadPos_;
    int bytesLeft = dataAvailable;

    // Temporary buffer for interleaved decoded audio
    // MP3 outputs up to 1152 samples per channel, stereo = 2304 samples
    static int16_t tempBuffer[2304];

    // Decode one frame
    int err = MP3Decode(decoder_, &inPtr, &bytesLeft, tempBuffer, 0);

    if (err != 0) {
        // Decode error - skip this byte and try again
        decodeErrors_++;
        frameBufferReadPos_++;
        if (err == -1 || err == -2) {
            // Underflow - need more data
            fillFrameBuffer();
        }
        return true;  // Try again
    }

    // Update read position
    size_t bytesConsumed = dataAvailable - bytesLeft;
    frameBufferReadPos_ += bytesConsumed;

    // Get info about decoded frame
    MP3GetLastFrameInfo(decoder_, frameInfo_);

    // outputSamps is TOTAL samples output, not per channel
    // For stereo: outputSamps = 2304 (1152 L + 1152 R interleaved)
    // For mono: outputSamps = 1152
    size_t totalOutputSamples = frameInfo_->outputSamps;
    size_t samplesPerChannel = (frameInfo_->nChans == 2) ?
                               (totalOutputSamples / 2) : totalOutputSamples;

    // De-interleave into ring buffer
    // Do most work outside IRQ-disabled section to minimize audio disruption
    // Only protect the pointer updates, not the data copies (PSRAM access is slow)
    size_t writePos = bufferWritePos_;

    if (frameInfo_->nChans == 2) {
        for (size_t i = 0; i < samplesPerChannel; i++) {
            decodedBufferLeft_[writePos] = tempBuffer[i * 2];
            decodedBufferRight_[writePos] = tempBuffer[i * 2 + 1];
            writePos = (writePos + 1) % BUFFER_SAMPLES;
        }
    } else {
        // Mono: duplicate to both channels
        for (size_t i = 0; i < samplesPerChannel; i++) {
            decodedBufferLeft_[writePos] = tempBuffer[i];
            decodedBufferRight_[writePos] = tempBuffer[i];
            writePos = (writePos + 1) % BUFFER_SAMPLES;
        }
    }

    // Only protect the atomic update of shared state
    __disable_irq();
    bufferWritePos_ = writePos;
    bufferAvailable_ += samplesPerChannel;
    __enable_irq();

    totalDecodedSamples_ += samplesPerChannel;
    return true;
}

bool AudioStreamFM9Mp3::seekToSample(uint32_t targetSample) {
    if (!decoder_ || !fileLoaded_) return false;

    Serial.printf("[AudioStreamFM9Mp3] Seeking to sample %u\n", targetSample);

    // Estimate byte position from sample (assumes relatively constant bitrate)
    // bytes = sample * (bitrate / 8) / samplerate
    float bytesPerSample = 0;
    if (frameInfo_ && frameInfo_->bitrate > 0 && frameInfo_->samprate > 0) {
        bytesPerSample = (frameInfo_->bitrate * 1000.0f / 8.0f) / frameInfo_->samprate;
    } else {
        // Fallback: assume 192kbps at 44.1kHz
        bytesPerSample = 24000.0f / 44100.0f;
    }

    uint32_t estimatedOffset = (uint32_t)(targetSample * bytesPerSample);

    // Clamp to file bounds (leave room for at least one frame)
    if (estimatedOffset > mp3Size_ - 1024) {
        estimatedOffset = mp3Size_ > 1024 ? mp3Size_ - 1024 : 0;
    }

    // Seek file
    file_.seek(baseOffset_ + estimatedOffset);
    fileReadPos_ = estimatedOffset;

    // Clear buffers
    __disable_irq();
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;
    currentSample_ = targetSample;
    __enable_irq();

    frameBufferFill_ = 0;
    frameBufferReadPos_ = 0;
    endOfFile_ = false;

    // Refill frame buffer
    fillFrameBuffer();

    // Find sync word in new data
    int syncOffset = MP3FindSyncWord(frameBuffer_, frameBufferFill_);
    if (syncOffset < 0) {
        Serial.println("[AudioStreamFM9Mp3] No sync after seek!");
        return false;
    }
    frameBufferReadPos_ = syncOffset;

    // Pre-fill decoded buffer
    for (int i = 0; i < 4; i++) {
        if (!decodeNextFrame()) break;
    }

    totalDecodedSamples_ = targetSample;
    seekCount_++;

    Serial.printf("[AudioStreamFM9Mp3] Seek complete, buffered %d samples\n", bufferAvailable_);
    return true;
}

void AudioStreamFM9Mp3::refillBuffer() {
    if (!fileLoaded_ || !decoder_) return;

    // Handle seek request from loop detection
    if (seekRequested_) {
        seekToSample(seekTargetSample_);
        seekRequested_ = false;
    }

    if (endOfFile_ && frameBufferFill_ - frameBufferReadPos_ == 0) return;

    // Decode ONE frame per call to match WAV's approach
    // This prevents blocking too long and gives audio ISR time to run
    // Main loop will call this repeatedly to keep buffer filled
    if (bufferAvailable_ < REFILL_THRESHOLD) {
        decodeNextFrame();
    }
}

// ============================================================================
// Audio Library ISR
// ============================================================================

void AudioStreamFM9Mp3::update() {
    if (!playing_ || paused_ || !decodedBufferLeft_ || !decodedBufferRight_) {
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
    if (bufferAvailable_ < AUDIO_BLOCK_SAMPLES + 1) {
        underruns_++;
        memset(left->data, 0, sizeof(left->data));
        memset(right->data, 0, sizeof(right->data));
        transmit(left, 0);
        transmit(right, 1);
        release(left);
        release(right);
        return;
    }

    // === GRADUAL RATE ADJUSTMENT SYNC ===
    // Same as WAV: adjust playback rate to maintain sync
    // - MP3 behind target: consume 129 input samples -> 128 output (speed up)
    // - MP3 ahead of target: consume 127 input samples -> 128 output (slow down)

    int8_t newSyncMode = 0;
    if (syncEnabled_) {
        int32_t drift = (int32_t)currentSample_ - (int32_t)targetSample_;

        if (drift < -SYNC_DEAD_ZONE) {
            newSyncMode = 1;  // Speed up
        } else if (drift > SYNC_DEAD_ZONE) {
            newSyncMode = -1;  // Slow down
        }

        if (newSyncMode == 1 && syncMode_ != 1) speedupCount_++;
        if (newSyncMode == -1 && syncMode_ != -1) slowdownCount_++;
    }
    syncMode_ = newSyncMode;

    int inputSamples = AUDIO_BLOCK_SAMPLES + syncMode_;  // 127, 128, or 129

    // Make sure we have enough samples
    if ((size_t)inputSamples > bufferAvailable_) {
        inputSamples = AUDIO_BLOCK_SAMPLES;
        syncMode_ = 0;
    }

    // Generate output using linear interpolation
    size_t startReadPos = bufferReadPos_;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        // Calculate fractional input position (fixed point: 16.16)
        uint32_t pos_fixed = ((uint32_t)i * ((inputSamples - 1) << 16)) / (AUDIO_BLOCK_SAMPLES - 1);
        uint32_t idx = pos_fixed >> 16;
        uint32_t frac = pos_fixed & 0xFFFF;

        size_t pos0 = (startReadPos + idx) % BUFFER_SAMPLES;
        size_t pos1 = (startReadPos + idx + 1) % BUFFER_SAMPLES;

        int16_t left0 = decodedBufferLeft_[pos0];
        int16_t left1 = decodedBufferLeft_[pos1];
        int16_t right0 = decodedBufferRight_[pos0];
        int16_t right1 = decodedBufferRight_[pos1];

        // Linear interpolation
        left->data[i] = left0 + (((int32_t)(left1 - left0) * (int32_t)frac) >> 16);
        right->data[i] = right0 + (((int32_t)(right1 - right0) * (int32_t)frac) >> 16);
    }

    // Advance buffer position
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
