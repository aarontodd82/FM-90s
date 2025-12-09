/**
 * @file audio_stream_dac.h
 * @brief AudioStream for Genesis DAC emulation
 *
 * CRITICAL: This class MUST be in its own translation unit to avoid ODR violations
 * with the Audio Library's update list registration. Pattern follows AudioStreamSPC.
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#ifndef AUDIO_STREAM_DAC_H
#define AUDIO_STREAM_DAC_H

#include <Audio.h>
#include <stdint.h>

// Forward declaration
class DACEmulator;

/**
 * @class AudioStreamDAC
 * @brief Custom AudioStream for Genesis DAC emulation output
 *
 * This class MUST be in its own translation unit to avoid ODR violations
 * with the Audio Library's update list registration system.
 *
 * Pattern: Identical to AudioStreamSPC
 * - Separate translation unit (critical!)
 * - Static stack allocation in main.cpp (critical!)
 * - No Serial.print in ISR (causes crashes)
 * - Always transmit blocks (even if silence)
 *
 * Thread Safety:
 * - update() called by Audio Library ISR at 44.1kHz (every 2.9ms)
 * - Must complete quickly (<2.9ms to avoid ISR overflow)
 * - No blocking operations allowed
 */
class AudioStreamDAC : public AudioStream {
public:
    /**
     * Constructor
     * @param emulator Pointer to DACEmulator (can be nullptr initially)
     */
    AudioStreamDAC(DACEmulator* emulator = nullptr);

    /**
     * Destructor
     */
    virtual ~AudioStreamDAC() = default;

    /**
     * Audio Library ISR callback
     * Called at 44.1kHz to fill audio blocks (128 samples each)
     * CRITICAL: No Serial.print allowed in this function!
     */
    void update() override;

    /**
     * Set the emulator pointer
     * For shared AudioStreamDAC pattern (like AudioStreamSPC)
     * @param emulator Pointer to DACEmulator
     */
    void setEmulator(DACEmulator* emulator);

    /**
     * Get update count (for diagnostics, checked from main loop)
     * @return Number of times update() has been called
     */
    uint32_t getUpdateCount() const { return updateCount_; }

    /**
     * Get tick count (volatile for ISR safety)
     * @return Volatile tick counter
     */
    volatile uint32_t getTicks() const { return ticks_; }

    // Prevent copies (critical for Audio Library registration)
    AudioStreamDAC(const AudioStreamDAC&) = delete;
    AudioStreamDAC& operator=(const AudioStreamDAC&) = delete;

private:
    DACEmulator* emulator_;    // Pointer to DAC emulator
    bool firstUpdate_;         // True on first update() call
    uint32_t updateCount_;     // Total update() calls (diagnostic)
    volatile uint32_t ticks_;  // Volatile tick counter (diagnostic)
};

#endif // AUDIO_STREAM_DAC_H
