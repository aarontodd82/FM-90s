/**
 * @file dac_emulator.cpp
 * @brief Implementation of YM2612 DAC emulator
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#include "dac_emulator.h"

DACEmulator::DACEmulator()
    : writePos_(0)
    , readPos_(0)
    , enabled_(false)
    , dacEnabled_(true)        // YM2612 DAC enabled by default
    , outputControl_(0xC0)     // Default: CENTER (both speakers)
    , lastSample_(0)           // Start with silence (center position)
    , underruns_(0)
    , overruns_(0) {

    // Clear ring buffer
    memset(ringBuffer_, 0, sizeof(ringBuffer_));

    Serial.println("[DACEmulator] Initialized");
    Serial.printf("[DACEmulator] Ring buffer size: %d samples (%.1f ms @ 44.1kHz)\n",
                  RING_BUFFER_SIZE, (RING_BUFFER_SIZE / 44.1f));
}

void DACEmulator::writeSample(uint8_t sample) {
    if (!enabled_) {
        return;  // Emulator disabled, drop sample
    }

    // Convert 8-bit unsigned → 16-bit signed
    int16_t sample16 = convert8to16(sample);

    // Update last sample (for sample-and-hold)
    lastSample_ = sample16;

    // Check if buffer has space
    if (space() == 0) {
        // Buffer full - overrun!
        overruns_++;
        return;  // Drop sample (but lastSample_ is still updated for hold)
    }

    // Write to ring buffer
    writeRaw(sample16);
}

void DACEmulator::prefillSilence(uint32_t samples) {
    // Clamp to buffer size - 1 (reserve 1 slot for full/empty distinction)
    if (samples >= RING_BUFFER_SIZE) {
        samples = RING_BUFFER_SIZE - 1;
    }

    // Fill with silence (0x80 = center in 8-bit, 0x0000 in 16-bit)
    for (uint32_t i = 0; i < samples; i++) {
        if (space() > 0) {
            writeRaw(0);  // Silence = 0x0000 (center)
        } else {
            break;  // Buffer full
        }
    }

    Serial.printf("[DACEmulator] Pre-filled %lu silence samples (%.1f ms)\n",
                  samples, (samples / 44.1f));
}

void DACEmulator::reset() {
    // Reset positions
    writePos_ = 0;
    readPos_ = 0;

    // Clear buffer
    memset(ringBuffer_, 0, sizeof(ringBuffer_));

    // Reset counters
    underruns_ = 0;
    overruns_ = 0;

    // Reset state to defaults
    dacEnabled_ = true;
    outputControl_ = 0xC0;  // Center
    lastSample_ = 0;        // Reset sample-and-hold to silence

    Serial.println("[DACEmulator] Reset complete");
}

void DACEmulator::fillAudioBuffer(int16_t* left, int16_t* right, size_t samples) {
    if (!enabled_ || !left || !right) {
        // Emulator disabled or invalid buffers - output silence
        if (left) memset(left, 0, samples * sizeof(int16_t));
        if (right) memset(right, 0, samples * sizeof(int16_t));
        return;
    }

    // Extract panning control bits
    bool leftEnable = (outputControl_ & 0x80) != 0;
    bool rightEnable = (outputControl_ & 0x40) != 0;

    // Fill audio buffers
    for (size_t i = 0; i < samples; i++) {
        int16_t sample;

        // Check if we have data to read
        if (available() > 0) {
            // Read from ring buffer
            sample = readRaw();
            // Update last sample for next hold period
            lastSample_ = sample;
        } else {
            // Buffer empty - use sample-and-hold!
            // This matches real YM2612 behavior: DAC holds the last written value
            // until a new sample is written
            underruns_++;
            sample = lastSample_;  // Hold last sample (NOT silence!)
        }

        // Apply stereo panning
        if (dacEnabled_) {
            // DAC enabled - apply panning
            left[i] = leftEnable ? sample : 0;
            right[i] = rightEnable ? sample : 0;
        } else {
            // DAC disabled - output silence
            left[i] = 0;
            right[i] = 0;
        }
    }
}

void DACEmulator::setDACEnabled(bool enabled) {
    dacEnabled_ = enabled;

    if (!enabled) {
        // DAC disabled - pre-fill buffer with silence to prevent clicks
        // This ensures smooth transition when DAC is re-enabled
        for (int i = 0; i < 64 && space() > 0; i++) {
            writeRaw(0);  // Silence
        }
    }
}

void DACEmulator::setOutputControl(uint8_t value) {
    outputControl_ = value;

    // Debug output disabled - was causing serial spam that slowed down VGM processing
    // Panning changes are very frequent in some VGMs (hundreds per second)
    // Each Serial.printf takes ~1ms, creating massive slowdown
}

uint32_t DACEmulator::getBufferLevel() const {
    return (uint32_t)available();
}

float DACEmulator::getBufferFillPercent() const {
    return (available() * 100.0f) / (RING_BUFFER_SIZE - 1);
}

void DACEmulator::resetCounters() {
    underruns_ = 0;
    overruns_ = 0;
}
