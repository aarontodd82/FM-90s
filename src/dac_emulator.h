/**
 * @file dac_emulator.h
 * @brief Software emulation of YM2612 DAC for Genesis VGM playback
 *
 * Provides zero-latency DAC sample writes via ring buffer, eliminating
 * hardware timing bottlenecks that cause glitches with dense PCM playback.
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * @class DACEmulator
 * @brief Software emulation of YM2612 DAC with ring buffer
 *
 * Key Features:
 * - Lock-free ring buffer (single producer/consumer)
 * - 8-bit unsigned → 16-bit signed sample conversion
 * - Stereo panning support (YM2612 register 0xB6)
 * - DAC enable state tracking (YM2612 register 0x2B bit 7)
 * - Underrun/overrun detection
 *
 * Thread Safety:
 * - writeSample() called from main loop (VGM player)
 * - fillAudioBuffer() called from Audio Library ISR
 * - Lock-free design (single producer, single consumer)
 */
class DACEmulator {
public:
    DACEmulator();
    ~DACEmulator() = default;

    // ========== Sample Writing (Main Loop Context) ==========

    /**
     * Write 8-bit unsigned PCM sample to ring buffer
     * @param sample 8-bit unsigned (0x00 = min, 0x80 = center, 0xFF = max)
     * @note Non-blocking, instant write (zero-latency)
     */
    void writeSample(uint8_t sample);

    /**
     * Pre-fill buffer with silence samples
     * Called at playback start to prevent initial underruns
     * @param samples Number of silence samples to pre-fill
     */
    void prefillSilence(uint32_t samples);

    /**
     * Reset buffer to empty state
     * Clears all samples and resets read/write positions
     */
    void reset();

    // ========== Audio Stream Interface (ISR Context) ==========

    /**
     * Fill stereo audio buffer with DAC samples
     * Called by AudioStreamDAC::update() in ISR context
     * Applies stereo panning based on YM2612 output control register
     * @param left Left channel buffer (128 samples)
     * @param right Right channel buffer (128 samples)
     * @param samples Number of samples to fill (typically 128)
     */
    void fillAudioBuffer(int16_t* left, int16_t* right, size_t samples);

    // ========== YM2612 Register State Tracking ==========

    /**
     * Set DAC enable state (YM2612 register 0x2B bit 7)
     * When disabled, output silence (prevents clicks)
     * @param enabled True to enable DAC, false to disable
     */
    void setDACEnabled(bool enabled);

    /**
     * Get current DAC enable state
     * @return True if DAC is enabled
     */
    bool isDACEnabled() const { return dacEnabled_; }

    /**
     * Set stereo output control (YM2612 register 0xB6)
     * Controls which speakers the DAC outputs to
     * @param value Register value:
     *   0xC0 (bits 7+6) = CENTER (both speakers)
     *   0x80 (bit 7)    = LEFT only
     *   0x40 (bit 6)    = RIGHT only
     *   0x00            = MUTED
     */
    void setOutputControl(uint8_t value);

    /**
     * Get current output control register value
     * @return Current output control (0x00-0xFF)
     */
    uint8_t getOutputControl() const { return outputControl_; }

    // ========== Configuration ==========

    /**
     * Enable or disable emulator
     * When disabled, fillAudioBuffer() outputs silence
     * @param enabled True to enable, false to disable
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * Check if emulator is enabled
     * @return True if enabled
     */
    bool isEnabled() const { return enabled_; }

    // ========== Diagnostics ==========

    /**
     * Get current buffer fill level
     * @return Number of samples in buffer (0-8192)
     */
    uint32_t getBufferLevel() const;

    /**
     * Get buffer fill percentage
     * @return Fill percentage (0.0-100.0)
     */
    float getBufferFillPercent() const;

    /**
     * Get underrun count
     * Underrun = ISR tried to read but buffer was empty
     * @return Total underruns since reset
     */
    uint32_t getUnderruns() const { return underruns_; }

    /**
     * Get overrun count
     * Overrun = Main loop tried to write but buffer was full
     * @return Total overruns since reset
     */
    uint32_t getOverruns() const { return overruns_; }

    /**
     * Reset diagnostic counters
     */
    void resetCounters();

private:
    // Ring buffer configuration
    static const size_t RING_BUFFER_SIZE = 8192;  // 185ms @ 44.1kHz
    static const size_t PREFILL_SAMPLES = 512;     // Default prefill: 11.6ms

    // Ring buffer storage (16-bit signed samples, ready for Audio Library)
    int16_t ringBuffer_[RING_BUFFER_SIZE];

    // Ring buffer positions (volatile for ISR safety)
    volatile size_t writePos_;  // Modified by main loop only
    volatile size_t readPos_;   // Modified by ISR only

    // State flags (volatile for ISR safety)
    volatile bool enabled_;           // Master enable/disable
    volatile bool dacEnabled_;        // YM2612 DAC enable (reg 0x2B bit 7)
    volatile uint8_t outputControl_;  // YM2612 output control (reg 0xB6)
    volatile int16_t lastSample_;     // Last written sample (for sample-and-hold)

    // Diagnostic counters
    uint32_t underruns_;  // Buffer empty when ISR tried to read
    uint32_t overruns_;   // Buffer full when main loop tried to write

    // ========== Private Helper Methods ==========

    /**
     * Convert 8-bit unsigned to 16-bit signed
     * YM2612: 0x00 = min, 0x80 = center, 0xFF = max
     * Teensy: -32768 = min, 0 = center, 32767 = max
     * @param sample 8-bit unsigned sample
     * @return 16-bit signed sample
     */
    inline int16_t convert8to16(uint8_t sample) const {
        return (int16_t)((sample - 128) * 256);
    }

    /**
     * Get number of samples available to read
     * @return Available samples (0 to RING_BUFFER_SIZE-1)
     */
    inline size_t available() const {
        size_t w = writePos_;
        size_t r = readPos_;
        return (w >= r) ? (w - r) : (RING_BUFFER_SIZE - r + w);
    }

    /**
     * Get number of free slots available to write
     * @return Free space (0 to RING_BUFFER_SIZE-1)
     */
    inline size_t space() const {
        // Reserve 1 slot to distinguish full from empty
        return RING_BUFFER_SIZE - available() - 1;
    }

    /**
     * Write single 16-bit sample to ring buffer (no overflow check)
     * @param sample 16-bit signed sample
     */
    inline void writeRaw(int16_t sample) {
        ringBuffer_[writePos_] = sample;
        writePos_ = (writePos_ + 1) % RING_BUFFER_SIZE;
    }

    /**
     * Read single 16-bit sample from ring buffer (no underflow check)
     * @return 16-bit signed sample
     */
    inline int16_t readRaw() {
        int16_t sample = ringBuffer_[readPos_];
        readPos_ = (readPos_ + 1) % RING_BUFFER_SIZE;
        return sample;
    }
};
