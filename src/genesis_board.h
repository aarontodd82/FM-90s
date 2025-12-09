/**
 * @file genesis_board.h
 * @brief Hardware abstraction layer for Sega Genesis synthesizer board (YM2612 + SN76489)
 *
 * This class provides low-level control of the Genesis sound chips via GPIO pins.
 * Supports FM synthesis (YM2612), PSG sound (SN76489), and PCM DAC playback.
 *
 * TIMING MODEL (simplified January 2025):
 * - All timing is unified and smart (tracks elapsed time between writes)
 * - YM2612: 4μs minimum between data writes (BUSY flag duration)
 * - SN76489: 9μs minimum between writes (32 PSG clocks @ 3.58MHz)
 * - Shift register settling: minimal (74HCT164 settles in ~40ns)
 * - Any time spent doing other work counts toward the wait
 *
 * @author Aaron
 * @date January 2025
 */

#pragma once
#include <Arduino.h>

class GenesisBoard {
public:
    /**
     * Pin configuration for Genesis board connections
     * Note: Clock signals (SN76489 @ 3.58MHz, YM2612 @ 7.68MHz) are generated
     * on the new board hardware, not by Teensy.
     */
    struct Config {
        uint8_t pinWrSN;   // SN76489 write strobe (active low)
        uint8_t pinWrYM;   // YM2612 write strobe (active low)
        uint8_t pinIcYM;   // YM2612 reset (active low)
        uint8_t pinA0YM;   // YM2612 address bit 0
        uint8_t pinA1YM;   // YM2612 address bit 1 (port select)
        uint8_t pinSCK;    // SPI clock for data transfer (directly to shift register)
        uint8_t pinSDI;    // SPI data input (MOSI to shift register)
    };

    /**
     * Initialize the Genesis board with pin configuration
     * @param config Pin configuration structure
     */
    void begin(const Config& config);

    /**
     * Reset both YM2612 and SN76489 chips to initial state
     */
    void reset();

    /**
     * Hardware reset of YM2612 (pulse reset pin)
     */
    void hardwareReset();

    // ========== SN76489 PSG Control ==========

    /**
     * Write data to SN76489 PSG chip
     * @param value 8-bit data to write
     */
    void writePSG(uint8_t value);

    /**
     * Silence all PSG channels
     */
    void silencePSG();

    /**
     * Set PSG attenuation mode (reduces PSG volume when playing with YM2612)
     * @param enable True to attenuate PSG, false for raw volume
     */
    void setPSGAttenuation(bool enable) { psgAttenuateForMix_ = enable; }

    // ========== YM2612 FM Control ==========

    /**
     * Write to YM2612 register
     * @param port Port number (0 or 1)
     * @param reg Register address
     * @param value Data to write
     */
    void writeYM2612(uint8_t port, uint8_t reg, uint8_t value);

    // ========== DAC Control ==========

    /**
     * Enable or disable YM2612 DAC mode on channel 6
     * @param enable True to enable DAC, false for normal FM mode
     */
    void enableDAC(bool enable);

    /**
     * Get current DAC enable state
     * @return True if DAC is enabled
     */
    bool isDACEnabled() const { return dacEnabled_; }

    /**
     * Write PCM sample to DAC (uses streaming mode automatically)
     * @param sample 8-bit unsigned PCM sample
     */
    void writeDAC(uint8_t sample);

    // ========== Utility Functions ==========

    /**
     * Get the last error message if any operation failed
     * @return Error message string or nullptr if no error
     */
    const char* getLastError() const { return lastError_; }

    /**
     * Enable or disable debug output for register writes
     * @param enable True to enable debug output
     */
    void setDebugMode(bool enable) { debugMode_ = enable; }

private:
    Config config_;
    bool dacEnabled_;
    bool dacStreamMode_;      // True if DAC address (0x2A) is latched, A0 is HIGH
    const char* lastError_;
    bool debugMode_;
    bool initialized_;

    // Unified timing state - tracks last write time for smart delays
    uint32_t lastWriteTime_;  // Microsecond timestamp of last completed write (PSG or YM data)

    // PSG volume attenuation (for blending with YM2612)
    bool psgAttenuateForMix_;

    // PSG attenuation lookup table (maps 0-15 attenuation to quieter values)
    static constexpr uint8_t PSG_ATTENUATION_MAP[16] = {
        2, 3, 4, 5, 6, 7, 8, 9,
       10,11,12,13,14,15,15,15
    };

    // ===== LOW-LEVEL PRIMITIVES =====

    /**
     * Bit-bang SPI transfer (MSB first)
     * Uses digitalWriteFast for speed. Takes ~2-3μs for 8 bits.
     */
    void spiTransfer(uint8_t data);

    /**
     * Reverse bits in a byte (for PSG data due to board wiring)
     */
    static uint8_t reverseBits(uint8_t data);

    /**
     * Validate port number
     */
    bool validatePort(uint8_t port);

    /**
     * Wait if necessary to meet timing requirements
     * @param minMicros Minimum microseconds since lastWriteTime_
     */
    inline void waitIfNeeded(uint32_t minMicros) {
        uint32_t now = micros();
        uint32_t elapsed = now - lastWriteTime_;
        if (elapsed < minMicros) {
            delayMicroseconds(minMicros - elapsed);
        }
    }

    /**
     * Enter DAC streaming mode (latch address 0x2A, set A0=HIGH)
     */
    void beginDACStream();

    /**
     * Exit DAC streaming mode (return A0 to LOW)
     */
    void endDACStream();

    // ===== TIMING CONSTANTS =====

    // YM2612 BUSY flag duration: 32 internal cycles @ ~1.28MHz (7.68MHz / 6) = ~25μs
    // But real-world testing shows 3 Z80 NOPs (~3.35μs) is enough
    // We use 5μs for safety margin
    static constexpr uint32_t YM_BUSY_US = 5;

    // SN76489 write timing: 32 PSG clocks @ 3.58MHz = ~9μs
    static constexpr uint32_t PSG_BUSY_US = 9;

    // YM2612 register addresses
    static constexpr uint8_t YM2612_DAC_DATA = 0x2A;
    static constexpr uint8_t YM2612_DAC_ENABLE = 0x2B;
};
