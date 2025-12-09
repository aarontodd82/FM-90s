/**
 * @file audio_stream_dac_prerender.cpp
 * @brief Implementation of AudioStreamDACPrerender
 *
 * CRITICAL: This file is a separate translation unit to avoid ODR violations
 * with the Audio Library's update list registration system.
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#include <Audio.h>  // MUST be first to ensure consistent AudioStream definition
#include "audio_stream_dac_prerender.h"
#include "dac_prerender.h"  // For file format constants

// ============================================================================
// Constructor / Destructor
// ============================================================================

AudioStreamDACPrerender::AudioStreamDACPrerender()
    : AudioStream(0, nullptr)  // 0 inputs means we're a source
    , fileLoaded_(false)
    , totalSamples_(0)
    , loopPointSample_(DACPrerenderer::NO_LOOP)
    , currentSample_(0)
    , playing_(false)
    , paused_(false)
    , loopEnabled_(true)
    , bufferReadPos_(0)
    , bufferWritePos_(0)
    , bufferAvailable_(0)
    , fileReadSample_(0)
    , endOfFile_(false)
    , needsSeek_(false)
    , targetSample_(0)
    , underruns_(0) {

    Serial.println("[AudioStreamDACPrerender] Constructor - registering with Audio Library");
    Serial.printf("[AudioStreamDACPrerender] Object created at address 0x%08X\n", (uint32_t)this);
    Serial.printf("[AudioStreamDACPrerender] Buffer size: %d samples (%.1f ms)\n",
                  BUFFER_SAMPLES, BUFFER_SAMPLES / 44.1f);
}

AudioStreamDACPrerender::~AudioStreamDACPrerender() {
    closeFile();
}

// ============================================================================
// File Management
// ============================================================================

bool AudioStreamDACPrerender::loadFile(const char* path) {
    // Close any existing file
    closeFile();

    if (!path || path[0] == '\0') {
        Serial.println("[AudioStreamDACPrerender] ERROR: Empty path");
        return false;
    }

    // Open file
    file_ = SD.open(path, FILE_READ);
    if (!file_) {
        Serial.printf("[AudioStreamDACPrerender] ERROR: Failed to open %s\n", path);
        return false;
    }

    // Read and validate header
    if (!readHeader()) {
        file_.close();
        return false;
    }

    fileLoaded_ = true;
    currentSample_ = 0;
    fileReadSample_ = 0;
    endOfFile_ = false;
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;

    Serial.printf("[AudioStreamDACPrerender] Loaded: %lu samples (%.2f sec)\n",
                  totalSamples_, totalSamples_ / 44100.0f);
    if (loopPointSample_ != DACPrerenderer::NO_LOOP) {
        Serial.printf("[AudioStreamDACPrerender] Loop point: %lu samples (%.2f sec)\n",
                      loopPointSample_, loopPointSample_ / 44100.0f);
    }

    // Pre-fill buffer before playback
    refillBuffer();

    return true;
}

void AudioStreamDACPrerender::closeFile() {
    stop();

    if (file_) {
        file_.close();
    }

    fileLoaded_ = false;
    totalSamples_ = 0;
    loopPointSample_ = DACPrerenderer::NO_LOOP;
    currentSample_ = 0;
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;
    fileReadSample_ = 0;
    endOfFile_ = false;
    needsSeek_ = false;
}

bool AudioStreamDACPrerender::readHeader() {
    uint8_t header[DACPrerenderer::HEADER_SIZE];

    size_t bytesRead = file_.read(header, DACPrerenderer::HEADER_SIZE);
    if (bytesRead != DACPrerenderer::HEADER_SIZE) {
        Serial.println("[AudioStreamDACPrerender] ERROR: Failed to read header");
        return false;
    }

    // Check magic
    if (header[0] != 'D' || header[1] != 'A' || header[2] != 'C' || header[3] != '1') {
        Serial.println("[AudioStreamDACPrerender] ERROR: Invalid magic (not DAC1)");
        return false;
    }

    // Read total samples (little-endian)
    totalSamples_ = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);

    // Read loop point (little-endian)
    loopPointSample_ = header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24);

    // Validate
    if (totalSamples_ == 0) {
        Serial.println("[AudioStreamDACPrerender] ERROR: File has 0 samples");
        return false;
    }

    // Check file size matches expected
    size_t expectedSize = DACPrerenderer::HEADER_SIZE + (totalSamples_ * 2);
    size_t actualSize = file_.size();
    if (actualSize < expectedSize) {
        Serial.printf("[AudioStreamDACPrerender] WARNING: File size %u < expected %u\n",
                      actualSize, expectedSize);
        // Adjust totalSamples to match actual file
        totalSamples_ = (actualSize - DACPrerenderer::HEADER_SIZE) / 2;
    }

    return true;
}

// ============================================================================
// Playback Control
// ============================================================================

void AudioStreamDACPrerender::play() {
    if (!fileLoaded_) return;

    // Reset to beginning
    currentSample_ = 0;
    fileReadSample_ = 0;
    targetSample_ = 0;  // Sync target starts at 0
    endOfFile_ = false;
    needsSeek_ = false;
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;

    // Seek file to data start
    file_.seek(DACPrerenderer::HEADER_SIZE);

    // Pre-fill buffer before playback starts
    refillBuffer();

    paused_ = false;
    playing_ = true;

    Serial.println("[AudioStreamDACPrerender] Playback started");
}

void AudioStreamDACPrerender::stop() {
    playing_ = false;
    paused_ = false;
    currentSample_ = 0;

    Serial.println("[AudioStreamDACPrerender] Playback stopped");
}

void AudioStreamDACPrerender::pause() {
    if (playing_) {
        paused_ = true;
        Serial.println("[AudioStreamDACPrerender] Playback paused");
    }
}

void AudioStreamDACPrerender::resume() {
    if (playing_ && paused_) {
        paused_ = false;
        Serial.println("[AudioStreamDACPrerender] Playback resumed");
    }
}

void AudioStreamDACPrerender::seekToLoop() {
    if (!fileLoaded_ || loopPointSample_ == DACPrerenderer::NO_LOOP) {
        Serial.println("[AudioStreamDACPrerender] seekToLoop: No file or no loop point");
        return;
    }

    // Calculate file position for loop point
    uint32_t filePos = DACPrerenderer::HEADER_SIZE + (loopPointSample_ * 2);

    Serial.printf("[AudioStreamDACPrerender] Seeking to loop point: sample %lu, file pos %lu\n",
                  loopPointSample_, filePos);

    // Seek file
    if (!file_.seek(filePos)) {
        Serial.println("[AudioStreamDACPrerender] ERROR: Failed to seek to loop point");
        stop();
        return;
    }

    // Update positions - use atomic disable to prevent ISR from reading stale values
    __disable_irq();
    currentSample_ = loopPointSample_;
    fileReadSample_ = loopPointSample_;
    endOfFile_ = false;
    needsSeek_ = false;  // We just did the seek
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;
    __enable_irq();

    // Immediately refill buffer since we're in main context (not ISR)
    refillBuffer();

    Serial.printf("[AudioStreamDACPrerender] Looped to sample %lu, buffer refilled\n", loopPointSample_);
}

float AudioStreamDACPrerender::getProgress() const {
    if (totalSamples_ == 0) return 0.0f;
    return (float)currentSample_ / (float)totalSamples_;
}

bool AudioStreamDACPrerender::seekToSample(uint32_t sample) {
    if (!fileLoaded_ || sample >= totalSamples_) {
        return false;
    }

    // Calculate file position
    uint32_t filePos = DACPrerenderer::HEADER_SIZE + (sample * 2);

    // Seek file
    if (!file_.seek(filePos)) {
        return false;
    }

    // Update positions
    currentSample_ = sample;
    fileReadSample_ = sample;
    endOfFile_ = false;

    // Clear buffer
    bufferReadPos_ = 0;
    bufferWritePos_ = 0;
    bufferAvailable_ = 0;

    return true;
}

// ============================================================================
// Synchronization
// ============================================================================

void AudioStreamDACPrerender::setTargetSample(uint32_t targetSample) {
    targetSample_ = targetSample;
}

int32_t AudioStreamDACPrerender::getSyncDrift() const {
    return (int32_t)currentSample_ - (int32_t)targetSample_;
}

// ============================================================================
// Buffer Management
// ============================================================================

size_t AudioStreamDACPrerender::available() const {
    return bufferAvailable_;
}

size_t AudioStreamDACPrerender::space() const {
    return BUFFER_SAMPLES - bufferAvailable_;
}

bool AudioStreamDACPrerender::needsRefill() const {
    return fileLoaded_ && playing_ && !endOfFile_ && (bufferAvailable_ < REFILL_THRESHOLD);
}

size_t AudioStreamDACPrerender::getBufferLevel() const {
    return bufferAvailable_;
}

void AudioStreamDACPrerender::refillBuffer() {
    // This is called from main loop, NOT from ISR
    // Safe to do SD card reads here

    if (!fileLoaded_ || !file_) {
        return;
    }

    // Check if ISR requested a seek (for looping)
    if (needsSeek_) {
        // Calculate file position for the loop point
        uint32_t filePos = DACPrerenderer::HEADER_SIZE + (fileReadSample_ * 2);
        if (file_.seek(filePos)) {
            Serial.printf("[AudioStreamDACPrerender] Seeked to loop point (sample %lu)\n", fileReadSample_);
        } else {
            Serial.println("[AudioStreamDACPrerender] ERROR: Failed to seek for loop!");
        }
        needsSeek_ = false;
        endOfFile_ = false;  // Reset end of file flag since we looped
    }

    if (endOfFile_) {
        return;
    }

    // Calculate how many samples we can read
    size_t freeSpace = space();
    if (freeSpace < 128) {
        return;  // Not enough space to bother
    }

    // Calculate how many samples remain in file
    uint32_t samplesRemaining = totalSamples_ - fileReadSample_;
    if (samplesRemaining == 0) {
        endOfFile_ = true;
        return;
    }

    // Read up to freeSpace samples (or remaining file, whichever is less)
    size_t samplesToRead = (freeSpace < samplesRemaining) ? freeSpace : samplesRemaining;

    // Read directly into buffer at write position
    // Handle wraparound
    size_t writePos = bufferWritePos_;
    size_t samplesRead = 0;

    while (samplesRead < samplesToRead) {
        // Calculate contiguous space from write position to end or read position
        size_t contiguous = BUFFER_SAMPLES - writePos;
        size_t toRead = (samplesToRead - samplesRead);
        if (toRead > contiguous) toRead = contiguous;

        // Read from file (2 bytes per sample)
        size_t bytesToRead = toRead * 2;
        size_t bytesRead = file_.read(readBuffer_ + (writePos * 2), bytesToRead);

        if (bytesRead == 0) {
            // End of file or error
            endOfFile_ = true;
            break;
        }

        size_t samplesActuallyRead = bytesRead / 2;
        samplesRead += samplesActuallyRead;
        writePos = (writePos + samplesActuallyRead) % BUFFER_SAMPLES;
        fileReadSample_ += samplesActuallyRead;

        if (bytesRead < bytesToRead) {
            // Partial read - end of file
            endOfFile_ = true;
            break;
        }
    }

    // Update write position and available count atomically-ish
    // (ISR only reads, main loop only writes)
    __disable_irq();
    bufferWritePos_ = writePos;
    bufferAvailable_ += samplesRead;
    __enable_irq();
}

// ============================================================================
// Audio Library Interface (ISR Context!)
// ============================================================================

void AudioStreamDACPrerender::update() {
    // CRITICAL: This runs in ISR context at 44.1 kHz
    // No SD card access, no Serial.print, minimal processing

    // Always allocate blocks
    audio_block_t* left = allocate();
    audio_block_t* right = allocate();

    if (!left || !right) {
        if (left) release(left);
        if (right) release(right);
        return;
    }

    // Check if we should output audio
    if (!playing_ || paused_ || !fileLoaded_) {
        // Output silence
        memset(left->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(right->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        transmit(left, 0);
        transmit(right, 1);
        release(left);
        release(right);
        return;
    }

    // === SYNCHRONIZATION ===
    // Check drift between our position and the VGM player's target position.
    // If we're behind, skip samples to catch up. If ahead, we'll naturally slow down.
    uint32_t target = targetSample_;  // Read volatile once

    // Skip samples if we're significantly behind (more than half a block)
    // This catches us up without audible glitches
    if (currentSample_ + 64 < target && bufferAvailable_ > 0) {
        uint32_t samplesToSkip = target - currentSample_;
        // Limit skip to what's available in buffer, and leave some for output
        if (samplesToSkip > bufferAvailable_ - AUDIO_BLOCK_SAMPLES) {
            samplesToSkip = (bufferAvailable_ > AUDIO_BLOCK_SAMPLES) ?
                            bufferAvailable_ - AUDIO_BLOCK_SAMPLES : 0;
        }
        // Also limit to prevent huge jumps (max 1 block worth of skip per ISR)
        if (samplesToSkip > AUDIO_BLOCK_SAMPLES) {
            samplesToSkip = AUDIO_BLOCK_SAMPLES;
        }

        // Skip the samples
        bufferReadPos_ = (bufferReadPos_ + samplesToSkip) % BUFFER_SAMPLES;
        bufferAvailable_ -= samplesToSkip;
        currentSample_ += samplesToSkip;
    }

    // Fill audio block from ring buffer
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        if (bufferAvailable_ > 0) {
            // Read current sample from buffer
            size_t pos = bufferReadPos_ * 2;
            uint8_t dacValue = readBuffer_[pos];
            uint8_t flags = readBuffer_[pos + 1];

            // Convert to 16-bit
            int16_t currentSample = ((int16_t)dacValue - 128) * 256;

            // Check DAC enable flag
            bool dacEnabled = (flags & DACPrerenderer::FLAG_DAC_ENABLED) != 0;
            if (!dacEnabled) {
                currentSample = 0;
            }

            // Apply panning
            uint8_t panBits = flags & DACPrerenderer::FLAG_PAN_MASK;
            bool leftEnable = (panBits == DACPrerenderer::FLAG_PAN_LEFT ||
                               panBits == DACPrerenderer::FLAG_PAN_CENTER);
            bool rightEnable = (panBits == DACPrerenderer::FLAG_PAN_RIGHT ||
                                panBits == DACPrerenderer::FLAG_PAN_CENTER);

            left->data[i] = leftEnable ? currentSample : 0;
            right->data[i] = rightEnable ? currentSample : 0;

            // Advance buffer position
            bufferReadPos_ = (bufferReadPos_ + 1) % BUFFER_SAMPLES;
            bufferAvailable_--;
            currentSample_++;
        } else {
            // Buffer underrun - check if we're ahead of target (that's OK, just output silence)
            // If we're behind, this is a real underrun
            if (currentSample_ < target) {
                underruns_++;
            }
            left->data[i] = 0;
            right->data[i] = 0;
        }

        // Check for end of track
        // NOTE: We do NOT auto-loop here! The VGM player controls looping by calling seekToLoop().
        // The DAC prerender stream just outputs whatever is in its buffer.
        // If we run out of data, just output silence - VGM player will tell us to loop if needed.
        if (currentSample_ >= totalSamples_) {
            // Output silence for the rest of this block
            for (int j = i + 1; j < AUDIO_BLOCK_SAMPLES; j++) {
                left->data[j] = 0;
                right->data[j] = 0;
            }
            break;
        }
    }

    // Always transmit blocks
    transmit(left, 0);
    transmit(right, 1);
    release(left);
    release(right);
}
