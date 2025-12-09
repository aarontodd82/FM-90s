#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <cstdint>

// Game Boy APU Emulator - VGM Backend
// Implements AudioStream for Teensy Audio Library integration
// Follows NES APU patterns for consistency

class GameBoyAPU : public AudioStream {
public:
    GameBoyAPU();
    virtual ~GameBoyAPU();

    // Reset APU to power-on state
    void reset();

    // Write to APU register (VGM mapping: reg 0x00 = GB $FF10)
    // Register $00-$3F maps to Game Boy $FF10-$FF4F
    void writeRegister(uint8_t reg, uint8_t value);

    // AudioStream interface - called by Teensy Audio Library at 44.1kHz
    virtual void update() override;

    // Public stopping flag for external access
    volatile bool stopping_;

    // Stop the frame sequencer timer safely (call BEFORE deleting!)
    void stopFrameTimer();

    // Start the frame sequencer timer (call when playback begins)
    void startFrameTimer();

private:
    // Master clock and sample rate (Game Boy DMG: 4.194304 MHz)
    static constexpr float MASTER_CLOCK_HZ = 4194304.0f;
    static constexpr float SAMPLE_RATE = 44100.0f;
    static constexpr float CLOCKS_PER_SAMPLE = MASTER_CLOCK_HZ / SAMPLE_RATE;  // ~95.1

    // Timer clock (channels 1-3 clock at master / 2 = 2.097152 MHz)
    static constexpr float TIMER_CLOCK_HZ = MASTER_CLOCK_HZ / 2.0f;
    static constexpr float TIMER_CLOCKS_PER_SAMPLE = TIMER_CLOCK_HZ / SAMPLE_RATE;  // ~47.6

    // Pulse Channel (CH1 and CH2)
    struct PulseChannel {
        // Timer (11-bit period)
        uint16_t timerPeriod;      // (2048 - frequency) * 4
        float timerCounter;        // Current countdown (sub-sample accuracy)

        // Duty cycle (8-step sequence, 0-3)
        uint8_t dutyCycle;         // 0=12.5%, 1=25%, 2=50%, 3=75%
        uint8_t dutyPosition;      // Position in 8-step sequence (0-7)

        // Band-limiting
        uint8_t lastOutput;        // Previous output for interpolation
        float outputBlend;         // Blend factor for smooth transitions

        // Volume/Envelope
        uint8_t volume;            // Initial volume from NRx2 bits 7-4 (0-15)
        bool constantVolume;       // Not used on GB (always use envelope)
        uint8_t envelopeCounter;   // Current envelope level (0-15)
        uint8_t envelopePeriod;    // Period from NRx2 bits 2-0 (0-7)
        uint8_t envelopeDivider;   // Countdown to next envelope tick
        bool envelopeDirection;    // False = decrease, true = increase
        bool envelopeRunning;      // Envelope active

        // Length counter
        uint8_t lengthCounter;     // 0-64, clocked at 256 Hz
        bool lengthEnabled;        // From NRx4 bit 6

        // Sweep (CH1 only)
        bool hasSweep;             // True for CH1, false for CH2
        uint8_t sweepPeriod;       // From NR10 bits 6-4 (0-7)
        uint8_t sweepShift;        // From NR10 bits 2-0 (0-7)
        bool sweepNegate;          // From NR10 bit 3
        uint8_t sweepDivider;      // Countdown to next sweep tick
        bool sweepEnabled;         // Sweep active
        uint16_t shadowFrequency;  // Internal frequency for sweep calculations
        bool sweepHasNegated;      // Tracks if negate was used (for direction change quirk)

        // Enable/DAC
        bool dacEnabled;           // True if (NRx2 & 0xF8) != 0
        bool enabled;              // From NR52 bits 0-1 (read-only status)

        void reset() {
            timerPeriod = 0;
            timerCounter = 0;
            dutyCycle = 0;
            dutyPosition = 0;
            lastOutput = 0;
            outputBlend = 0;
            volume = 0;
            constantVolume = false;
            envelopeCounter = 0;
            envelopePeriod = 0;
            envelopeDivider = 0;
            envelopeDirection = false;
            envelopeRunning = false;
            lengthCounter = 0;
            lengthEnabled = false;
            hasSweep = false;
            sweepPeriod = 0;
            sweepShift = 0;
            sweepNegate = false;
            sweepDivider = 0;
            sweepEnabled = false;
            shadowFrequency = 0;
            sweepHasNegated = false;
            dacEnabled = false;
            enabled = false;
        }

        // Get current output (0-15) - volume-scaled
        uint8_t getOutput();

        // Get raw waveform state (0 or 1) - for band-limiting
        uint8_t getRawWaveform();

        // Clock the timer (called ~47.6 times per sample)
        void clockTimer();

        // Clock the length counter (called at 256 Hz from frame sequencer)
        void clockLength();

        // Clock the envelope (called at 64 Hz from frame sequencer)
        void clockEnvelope();

        // Clock the sweep unit (called at 128 Hz from frame sequencer, CH1 only)
        void clockSweep();

        // Calculate sweep target frequency and check for overflow/muting
        uint16_t calculateSweepTarget();
        void updateSweepMuting();

        // Trigger channel (NRx4 bit 7 = 1)
        void trigger(uint16_t frequency);
    };

    // Wave Channel (CH3)
    struct WaveChannel {
        // Wave RAM (16 bytes = 32×4-bit samples)
        uint8_t waveRam[16];
        uint8_t samplePosition;    // 0-31 (which sample to play next)
        uint8_t lastSample;        // Last played sample for band-limiting

        // Timer (11-bit period, but HALF the pulse period!)
        uint16_t timerPeriod;      // (2048 - frequency) * 2
        float timerCounter;        // Current countdown

        // Volume shift (0-3)
        uint8_t volumeShift;       // From NR32 bits 6-5: 0=mute, 1=100%, 2=50%, 3=25%

        // Length counter
        uint16_t lengthCounter;    // 0-256, clocked at 256 Hz
        bool lengthEnabled;        // From NR34 bit 6

        // Enable/DAC
        bool dacEnabled;           // From NR30 bit 7
        bool enabled;              // From NR52 bit 2

        void reset() {
            memset(waveRam, 0, sizeof(waveRam));
            samplePosition = 0;
            lastSample = 0;
            timerPeriod = 0;
            timerCounter = 0;
            volumeShift = 0;
            lengthCounter = 0;
            lengthEnabled = false;
            dacEnabled = false;
            enabled = false;
        }

        // Clock the timer (called ~47.6 times per sample, same as pulse)
        void clockTimer();

        // Clock the length counter (called at 256 Hz from frame sequencer)
        void clockLength();

        // Get current 4-bit sample from wave RAM
        uint8_t getCurrentSample();

        // Get current output (0-15) with volume shift applied
        uint8_t getOutput();

        // Trigger channel (NR34 bit 7 = 1)
        void trigger(uint16_t frequency);
    };

    // Noise Channel (CH4)
    struct NoiseChannel {
        // LFSR (15-bit or 7-bit)
        uint16_t lfsr;             // Bit 14-0 used, bit 0 = output (inverted!)
        bool widthMode;            // False = 15-bit (32767 period), true = 7-bit (127 period)

        // Timer
        uint8_t divisorCode;       // 0-7 -> lookup table
        uint8_t clockShift;        // 0-15 (shift left)
        float timerCounter;        // Current countdown

        // Volume/Envelope (same as pulse channels)
        uint8_t volume;            // Initial volume from NR42 bits 7-4 (0-15)
        bool constantVolume;       // Not used on GB
        uint8_t envelopeCounter;   // Current envelope level (0-15)
        uint8_t envelopePeriod;    // Period from NR42 bits 2-0 (0-7)
        uint8_t envelopeDivider;   // Countdown to next envelope tick
        bool envelopeDirection;    // False = decrease, true = increase
        bool envelopeRunning;      // Envelope active

        // Length counter
        uint8_t lengthCounter;     // 0-64, clocked at 256 Hz
        bool lengthEnabled;        // From NR44 bit 6

        // Enable/DAC
        bool dacEnabled;           // True if (NR42 & 0xF8) != 0
        bool enabled;              // From NR52 bit 3

        void reset() {
            lfsr = 0x7FFF;  // All bits set (power-on state)
            widthMode = false;
            divisorCode = 0;
            clockShift = 0;
            timerCounter = 0;
            volume = 0;
            constantVolume = false;
            envelopeCounter = 0;
            envelopePeriod = 0;
            envelopeDivider = 0;
            envelopeDirection = false;
            envelopeRunning = false;
            lengthCounter = 0;
            lengthEnabled = false;
            dacEnabled = false;
            enabled = false;
        }

        // Shift the LFSR (called when timer expires)
        void shiftLFSR();

        // Clock the timer (variable rate based on divisor/shift)
        void clockTimer();

        // Clock the envelope (64 Hz from frame sequencer)
        void clockEnvelope();

        // Clock the length counter (256 Hz from frame sequencer)
        void clockLength();

        // Get timer period in timer clocks
        uint16_t getTimerPeriod();

        // Get current output (0-15)
        uint8_t getOutput();

        // Trigger channel (NR44 bit 7 = 1)
        void trigger();
    };

    // APU state
    uint8_t registers_[0x40];   // $FF10-$FF4F (VGM reg 0x00-0x3F)
    PulseChannel pulse1_;
    PulseChannel pulse2_;
    WaveChannel wave_;
    NoiseChannel noise_;

    // Global control registers
    bool apuEnabled_;            // NR52 bit 7 (master power)
    uint8_t panningLeft_;        // NR51 bits 7-4 (CH4, CH3, CH2, CH1 left enable)
    uint8_t panningRight_;       // NR51 bits 3-0 (CH4, CH3, CH2, CH1 right enable)
    uint8_t volumeLeft_;         // NR50 bits 6-4 (0-7)
    uint8_t volumeRight_;        // NR50 bits 2-0 (0-7)

    // Frame sequencer (512 Hz, drives all timing)
    IntervalTimer frameTimer_;
    static GameBoyAPU* instance_;  // For ISR access
    volatile uint8_t frameStep_;   // 0-7 (8-step sequence)

    // Clock accumulator for sub-sample accuracy
    float clockAccumulator_;

    // Frame sequencer ISR and tick logic
    static void frameSequencerISR();  // ISR callback (must be static)
    void frameSequencerTick();        // Frame sequencer logic (512 Hz)

    // Duty cycle sequences (from Pan Docs)
    static const uint8_t dutySequences_[4][8];

    // Noise divisor lookup table
    static const uint8_t divisorTable_[8];

    // Stereo mixing with panning (GB hardware panning)
    void mixChannelsStereo(
        uint8_t pulse1Out, uint8_t pulse2Out,
        uint8_t waveOut, uint8_t noiseOut,
        float& outLeft, float& outRight
    );

    // Output filters (28Hz HPF only - DMG hardware has no documented LPF)
    float hpf90_a_;  // HPF coefficient

    // Stereo filter state (HPF only)
    float hpf90_x1_left_, hpf90_y1_left_;       // 28Hz HPF state (left)
    float hpf90_x1_right_, hpf90_y1_right_;     // 28Hz HPF state (right)

    // Helper functions for applying output filters
    inline float applyOutputFiltersLeft(float x);    // Stereo left
    inline float applyOutputFiltersRight(float x);   // Stereo right

    // Debug
    uint32_t registerWriteCount_;
    uint32_t updateCallCount_;
    uint32_t nonZeroSampleCount_;
};
