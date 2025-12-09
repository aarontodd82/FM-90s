#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <cstdint>

// NES APU Emulator - Phases 2-5: Audio Framework + Basic Channels
// Implements AudioStream for Teensy Audio Library integration
// with basic pulse channel synthesis

class NESAPUEmulator : public AudioStream {
public:
    NESAPUEmulator();
    virtual ~NESAPUEmulator();  // CRITICAL: Must be virtual for proper AudioStream cleanup!

    // Reset APU to power-on state
    void reset();

    // Write to APU register ($4000-$4017 mapped to $00-$17)
    // Register mapping from VGM spec:
    //   $00-$1F -> $4000-$401F
    //   $20-$3E -> $4080-$409E
    //   $3F     -> $4023
    //   $40-$7F -> $4040-$407F
    void writeRegister(uint8_t reg, uint8_t value);

    // Load DPCM sample data from VGM data block (type 0x07)
    void loadDPCMData(const uint8_t* data, size_t size);

    // Ensure DPCM buffer is allocated
    void ensureDPCMBuffer();

    // Load DPCM data at specific offset (for type 0xC2)
    void loadDPCMDataAtOffset(const uint8_t* data, size_t size, uint16_t offset);

    // AudioStream interface - called by Teensy Audio Library at 44.1kHz
    virtual void update() override;

    // Public stopping flag for external access
    volatile bool stopping_;

    // Stop the frame counter timer safely (call BEFORE deleting!)
    void stopFrameTimer();

    // Start the frame counter timer (call when playback begins)
    void startFrameTimer();

private:
    // APU clock rate (NTSC)
    static constexpr float CPU_CLOCK_HZ = 1789773.0f;
    static constexpr float SAMPLE_RATE = 44100.0f;
    static constexpr float CPU_CLOCKS_PER_SAMPLE = CPU_CLOCK_HZ / SAMPLE_RATE;  // ~40.58

    // Pulse Channel State (basic implementation - Phase 5)
    struct PulseChannel {
        // Timer (11-bit period)
        uint16_t timerPeriod;      // Period value from registers
        float timerCounter;        // Current countdown (uses float for sub-sample accuracy)

        // Duty cycle (2 bits: 0-3)
        uint8_t dutyCycle;         // 0=12.5%, 1=25%, 2=50%, 3=75%
        uint8_t dutyPosition;      // Position in 8-step duty sequence (0-7)

        // Band-limiting via linear interpolation
        uint8_t lastOutput;        // Previous output for interpolation
        float outputBlend;         // Blend factor for smooth transitions

        // Volume (4 bits)
        uint8_t volume;            // 0-15 (constant volume parameter from register bits 3-0)
        bool constantVolume;       // True = use volume directly, false = use envelope

        // Envelope unit (Phase 6 - from NESdev wiki)
        bool envelopeStart;        // Start flag (set when writing $4003/$4007)
        uint8_t envelopeDivider;   // Divider counter
        uint8_t envelopePeriod;    // Divider reload value (V from bits 3-0)
        uint8_t envelopeDecay;     // Decay level counter (0-15)
        bool envelopeLoop;         // Loop flag (same as lengthHalt, bit 5)

        // Enable
        bool enabled;              // From $4015
        bool lengthHalt;           // From register bit 5 (prevents auto-silence)

        // Length counter (Phase 4 - CRITICAL for note duration!)
        uint8_t lengthCounter;     // Counts down at 120Hz, silences when 0

        // Sweep unit (Phase 7 - from NESdev wiki)
        bool sweepEnabled;         // Enable flag (bit 7 of $4001/$4005)
        uint8_t sweepDivider;      // Divider counter
        uint8_t sweepPeriod;       // Divider reload value (P from bits 6-4)
        bool sweepNegate;          // Negate flag (bit 3)
        uint8_t sweepShift;        // Shift count (bits 2-0)
        bool sweepReload;          // Reload flag (set when writing sweep register)
        bool sweepOnesComplement;  // True for Pulse1, false for Pulse2

        // Silencing conditions
        bool periodTooLow;         // Period < 8 silences channel
        bool sweepMuting;          // Sweep unit muting (target period > $7FF)

        void reset() {
            timerPeriod = 0;
            timerCounter = 1;  // Start at 1 to avoid immediate underflow
            dutyCycle = 0;
            dutyPosition = 0;
            lastOutput = 0;
            outputBlend = 0;
            volume = 0;
            constantVolume = true;
            enabled = false;
            lengthHalt = false;
            lengthCounter = 0;
            periodTooLow = false;

            // Envelope
            envelopeStart = false;
            envelopeDivider = 0;
            envelopePeriod = 0;
            envelopeDecay = 0;
            envelopeLoop = false;

            // Sweep
            sweepEnabled = false;
            sweepDivider = 0;
            sweepPeriod = 0;
            sweepNegate = false;
            sweepShift = 0;
            sweepReload = false;
            sweepOnesComplement = false;
            sweepMuting = false;
        }

        // Get current output (0-15) - volume-scaled
        uint8_t getOutput();

        // Get raw waveform state (0 or 1) - for band-limiting
        uint8_t getRawWaveform();

        // Clock the timer (called ~40 times per sample)
        void clockTimer();

        // Clock the length counter (called at 120Hz from frame counter)
        void clockLength();

        // Clock the envelope (called at 240Hz from frame counter) - Phase 6
        void clockEnvelope();

        // Clock the sweep unit (called at 120Hz from frame counter) - Phase 7
        void clockSweep();

        // Calculate sweep target period and check muting - Phase 7
        uint16_t calculateSweepTarget();
        void updateSweepMuting();
    };

    // Length counter lookup table (Phase 4 - from NESdev wiki)
    static const uint8_t lengthTable_[32];

    // Triangle Channel State (Phase 8)
    struct TriangleChannel {
        // Timer (11-bit period, like pulse but clocks at CPU rate not APU rate)
        uint16_t timerPeriod;      // Period value from registers
        float timerCounter;        // Current countdown (sub-sample accuracy)

        // Sequence position (32 steps)
        uint8_t sequenceStep;      // Position in triangle waveform (0-31)

        // Linear counter (separate from length counter)
        uint8_t linearCounter;     // Current counter value
        uint8_t linearReload;      // Reload value from register
        bool linearReloadFlag;     // Reload flag (set on $400B write)
        bool linearControl;        // Control flag (same as lengthHalt)

        // Length counter (like pulse channels)
        uint8_t lengthCounter;     // Counts down at 120Hz
        bool lengthHalt;           // Prevents auto-silence (same as linear control)

        // Enable
        bool enabled;              // From $4015

        // Silencing conditions
        bool periodTooLow;         // Period < 2 causes ultrasonic silencing

        void reset() {
            timerPeriod = 0;
            timerCounter = 0;
            sequenceStep = 0;
            linearCounter = 0;
            linearReload = 0;
            linearReloadFlag = false;
            linearControl = false;
            lengthCounter = 0;
            lengthHalt = false;
            enabled = false;
            periodTooLow = false;
        }

        // Clock the timer (called at CPU rate, not APU rate!)
        void clockTimer();

        // Clock the linear counter (called at 240Hz from frame counter)
        void clockLinearCounter();

        // Clock the length counter (called at 120Hz from frame counter)
        void clockLength();

        // Get current output (0-15)
        uint8_t getOutput();
    };

    // Noise Channel State (Phase 9)
    struct NoiseChannel {
        // Linear Feedback Shift Register (15-bit)
        uint16_t lfsr;             // 15-bit shift register (bit 14-0)

        // Timer (4-bit period index into lookup table)
        uint8_t periodIndex;       // Index into noisePeriodTable_ (0-15)
        float timerCounter;        // Current countdown

        // Mode
        bool mode;                 // false = normal (32767 steps), true = short (93/31 steps)

        // Volume/Envelope
        uint8_t volume;            // 4-bit volume (0-15)
        bool constantVolume;       // True = use volume directly, false = use envelope

        // Envelope unit (same as pulse channels)
        bool envelopeStart;        // Start flag (set when writing $400F)
        uint8_t envelopeDivider;   // Divider counter
        uint8_t envelopePeriod;    // Divider reload value (V from bits 3-0)
        uint8_t envelopeDecay;     // Decay level counter (0-15)
        bool envelopeLoop;         // Loop flag (same as lengthHalt, bit 5)

        // Length counter
        uint8_t lengthCounter;     // Counts down at 120Hz
        bool lengthHalt;           // Prevents auto-silence

        // Enable
        bool enabled;              // From $4015

        void reset() {
            lfsr = 1;  // Initialize to 1 (hardware power-up state)
            periodIndex = 0;
            timerCounter = 0;
            mode = false;
            volume = 0;
            constantVolume = true;
            envelopeStart = false;
            envelopeDivider = 0;
            envelopePeriod = 0;
            envelopeDecay = 0;
            envelopeLoop = false;
            lengthCounter = 0;
            lengthHalt = false;
            enabled = false;
        }

        // Shift the LFSR (called when timer expires)
        void shiftLFSR();

        // Clock the timer
        void clockTimer();

        // Clock the envelope (240Hz from frame counter)
        void clockEnvelope();

        // Clock the length counter (120Hz from frame counter)
        void clockLength();

        // Get current output (0-15)
        uint8_t getOutput();
    };

    // DMC Channel State (Phase 10)
    struct DMCChannel {
        // 7-bit output level counter ($00-$7F)
        uint8_t outputLevel;       // Current DAC output (0-127)

        // Sample playback
        const uint8_t* sampleData; // Pointer to DPCM sample data (from VGM data block)
        uint16_t sampleAddress;    // Current byte address in sample
        uint16_t sampleLength;     // Remaining bytes to play

        // Sample buffering
        uint8_t sampleBuffer;      // Current byte being played
        uint8_t bitsRemaining;     // Bits left in current byte (0-8)

        // Timer
        uint8_t rateIndex;         // Index into dmcRateTable_ (0-15)
        float timerCounter;        // Current countdown

        // Flags
        bool loop;                 // Loop sample when finished
        bool irqEnabled;          // IRQ on sample finish (not used in VGM)
        bool enabled;             // From $4015
        bool silence;             // Silence flag when sample finished

        // For VGM playback - store the entire sample in memory
        uint8_t* vgmSampleData;    // Allocated buffer for VGM DPCM data
        uint16_t vgmSampleSize;    // Total size of VGM sample
        uint16_t vgmStartAddress;  // Start address for looping
        uint16_t vgmConfiguredLength; // Configured sample length from $4013 (preserved for restart)

        void reset() {
            outputLevel = 0x40;  // Start at center (64) to avoid DC offset pop
            sampleData = nullptr;
            sampleAddress = 0;
            sampleLength = 0;
            sampleBuffer = 0;
            bitsRemaining = 0;
            rateIndex = 0;
            timerCounter = 0;
            loop = false;
            irqEnabled = false;
            enabled = false;
            silence = true;
            vgmSampleData = nullptr;
            vgmSampleSize = 0;
            vgmStartAddress = 0;
            vgmConfiguredLength = 0;
        }

        // Clock the timer
        void clockTimer();

        // Process next bit from sample buffer
        void processNextBit();

        // Start sample playback
        void startSample(uint16_t address, uint16_t length);

        // Get current output (0-127)
        uint8_t getOutput();
    };

    // APU state
    uint8_t registers_[0x18];  // $4000-$4017 register state
    PulseChannel pulse1_;
    PulseChannel pulse2_;
    TriangleChannel triangle_;
    NoiseChannel noise_;
    DMCChannel dmc_;

    // Frame counter (240Hz for envelope/length/sweep timing)
    IntervalTimer frameTimer_;
    static NESAPUEmulator* instance_;  // For ISR access
    volatile uint8_t frameStep_;       // Current frame step (0-3 or 0-4)
    volatile bool frameMode_;          // false = 4-step, true = 5-step
    volatile bool frameIRQDisable_;    // IRQ inhibit flag

    // Clock accumulator for sub-sample accuracy
    float clockAccumulator_;

    // APU cycle tracking (pulse channels clock every 2 CPU cycles)
    bool cpuCycleEven_;

    // Duty cycle sequences (from NESdev wiki)
    static const uint8_t dutySequences_[4][8];

    // Triangle waveform sequence (Phase 8 - from NESdev wiki)
    // 15, 14, 13...1, 0, 0, 1, 2...14, 15
    static const uint8_t triangleSequence_[32];

    // Noise period lookup table (Phase 9 - from NESdev wiki)
    static const uint16_t noisePeriodTable_[16];

    // DMC rate lookup table (Phase 10 - from NESdev wiki)
    static const uint16_t dmcRateTable_[16];

    // Debug
    uint32_t registerWriteCount_;
    uint32_t updateCallCount_;
    uint32_t nonZeroSampleCount_;

    // Frame counter ISR and tick logic
    static void frameCounterISR();  // ISR callback (must be static)
    void frameCounterTick();        // Frame counter logic (240Hz)

    // Nonlinear mixing (from NESdev wiki - CRITICAL for authentic sound!)
    float mixChannels(uint8_t pulse1Out, uint8_t pulse2Out, uint8_t triangleOut, uint8_t noiseOut, uint8_t dmcOut);

    // Stereo nonlinear mixing with panning (optional)
    // Pans pulse1 left, pulse2 right, noise by frequency
    void mixChannelsStereo(
        uint8_t pulse1Out, uint8_t pulse2Out,
        uint8_t triangleOut, uint8_t noiseOut, uint8_t dmcOut,
        uint8_t noisePeriodIndex,
        float& outLeft, float& outRight
    );

    // Simple first-order lowpass filter for reducing aliasing
    float lowpassFilterState_;
    static constexpr float LOWPASS_CUTOFF = 0.15f;  // Reduced: 0=no filtering, 1=heavy filtering

    // Analog output filters (90Hz HPF, 440Hz HPF, 14kHz LPF) from NESdev
    // These simulate the NES analog output path
    float hpf90_a_, hpf440_a_, lpf14k_a_;  // Filter coefficients (shared)

    // Mono filter state (when stereo disabled)
    float hpf90_x1_, hpf90_y1_;            // 90Hz HPF state
    float hpf440_x1_, hpf440_y1_;          // 440Hz HPF state
    float lpf14k_y1_;                      // 14kHz LPF state

    // Stereo filter state (when stereo enabled)
    float hpf90_x1_left_, hpf90_y1_left_;       // 90Hz HPF state (left)
    float hpf440_x1_left_, hpf440_y1_left_;     // 440Hz HPF state (left)
    float lpf14k_y1_left_;                      // 14kHz LPF state (left)
    float hpf90_x1_right_, hpf90_y1_right_;     // 90Hz HPF state (right)
    float hpf440_x1_right_, hpf440_y1_right_;   // 440Hz HPF state (right)
    float lpf14k_y1_right_;                     // 14kHz LPF state (right)

    // Helper functions for applying output filters
    inline float applyOutputFilters(float x);        // Mono (original)
    inline float applyOutputFiltersLeft(float x);    // Stereo left
    inline float applyOutputFiltersRight(float x);   // Stereo right
};
