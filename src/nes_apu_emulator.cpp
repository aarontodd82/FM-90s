#include "nes_apu_emulator.h"
#include "debug_config.h"

// Duty cycle LOOKUP TABLES from NESdev wiki
// CRITICAL: Sequencer reads in order 0, 7, 6, 5, 4, 3, 2, 1 (BACKWARDS!)
// These are the TABLE values, not the output waveform
const uint8_t NESAPUEmulator::dutySequences_[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  // Duty 0: 12.5% (outputs: 0 1 0 0 0 0 0 0)
    {0, 0, 0, 0, 0, 0, 1, 1},  // Duty 1: 25%   (outputs: 0 1 1 0 0 0 0 0)
    {0, 0, 0, 0, 1, 1, 1, 1},  // Duty 2: 50%   (outputs: 0 1 1 1 1 0 0 0)
    {1, 1, 1, 1, 1, 1, 0, 0}   // Duty 3: 25% negated (outputs: 1 0 0 1 1 1 1 1)
};

// Length counter lookup table from NESdev wiki (Phase 4)
// Maps 5-bit index ($00-$1F) to actual length counter values
const uint8_t NESAPUEmulator::lengthTable_[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

// Triangle waveform sequence (Phase 8)
// Produces triangle wave: 15, 14, 13...1, 0, 0, 1, 2...14, 15
const uint8_t NESAPUEmulator::triangleSequence_[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

// Noise period lookup table (Phase 9 - from NESdev wiki)
// NTSC values in CPU cycles
const uint16_t NESAPUEmulator::noisePeriodTable_[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

// DMC rate lookup table (Phase 10 - from NESdev wiki)
// NTSC values in CPU cycles
const uint16_t NESAPUEmulator::dmcRateTable_[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 84, 72, 54
};

// Register name lookup for debug output
static const char* getRegisterName(uint8_t reg) {
    if (reg >= 0x00 && reg <= 0x1F) {
        uint8_t nesReg = reg;
        switch (nesReg) {
            case 0x00: return "Pulse1_Duty";
            case 0x01: return "Pulse1_Sweep";
            case 0x02: return "Pulse1_TimerLo";
            case 0x03: return "Pulse1_Length";
            case 0x04: return "Pulse2_Duty";
            case 0x05: return "Pulse2_Sweep";
            case 0x06: return "Pulse2_TimerLo";
            case 0x07: return "Pulse2_Length";
            case 0x08: return "Triangle_Linear";
            case 0x0A: return "Triangle_TimerLo";
            case 0x0B: return "Triangle_Length";
            case 0x0C: return "Noise_Envelope";
            case 0x0E: return "Noise_Period";
            case 0x0F: return "Noise_Length";
            case 0x10: return "DMC_Flags";
            case 0x11: return "DMC_DirectLoad";
            case 0x12: return "DMC_SampleAddr";
            case 0x13: return "DMC_SampleLen";
            case 0x15: return "Status";
            case 0x17: return "FrameCounter";
            default: return "Unknown";
        }
    }
    return "Invalid";
}

// Static member initialization
NESAPUEmulator* NESAPUEmulator::instance_ = nullptr;

NESAPUEmulator::NESAPUEmulator()
    : AudioStream(0, nullptr)  // 0 inputs, stereo output created in update()
    , registerWriteCount_(0)
    , clockAccumulator_(0)
    , cpuCycleEven_(false)
    , updateCallCount_(0)
    , nonZeroSampleCount_(0)
    , frameStep_(0)
    , frameMode_(false)  // Start in 4-step mode
    , frameIRQDisable_(true)  // IRQ disabled by default
    , stopping_(false)  // Not stopping
    , lowpassFilterState_(0.0f) {

    // DIAGNOSTIC: We can't access next_update (it's private), but we can log
    // our object address for comparison
    Serial.printf("[NESAPUEmulator] DIAGNOSTIC: this=%p, AudioStream base initialized\n", this);

    memset(registers_, 0, sizeof(registers_));
    pulse1_.reset();
    pulse2_.reset();
    triangle_.reset();
    noise_.reset();
    dmc_.reset();

    // Set sweep ones' complement mode (Pulse 1 = true, Pulse 2 = false)
    pulse1_.sweepOnesComplement = true;   // Pulse 1 uses ones' complement
    pulse2_.sweepOnesComplement = false;  // Pulse 2 uses two's complement

    // Initialize analog output filters (90Hz HPF, 440Hz HPF, 14kHz LPF)
    const float fs = 44100.0f;
    hpf90_a_   = expf(-2.0f * 3.1415926535f *  90.0f / fs);
    hpf440_a_  = expf(-2.0f * 3.1415926535f * 440.0f / fs);
    lpf14k_a_  = 1.0f - expf(-2.0f * 3.1415926535f * 14000.0f / fs);

    // Initialize mono filter states
    hpf90_x1_ = hpf90_y1_ = 0.0f;
    hpf440_x1_ = hpf440_y1_ = 0.0f;
    lpf14k_y1_ = 0.0f;

    // Initialize stereo filter states
    hpf90_x1_left_ = hpf90_y1_left_ = 0.0f;
    hpf440_x1_left_ = hpf440_y1_left_ = 0.0f;
    lpf14k_y1_left_ = 0.0f;
    hpf90_x1_right_ = hpf90_y1_right_ = 0.0f;
    hpf440_x1_right_ = hpf440_y1_right_ = 0.0f;
    lpf14k_y1_right_ = 0.0f;

    instance_ = this;

    // NOTE: Frame timer is NOT started here - it must be started explicitly
    // by calling startFrameTimer() when playback begins

    // // Serial.println("==========================================================");
    // // Serial.println("NES APU: COMPLETE! ALL 5 CHANNELS - 2025-11-07 v10");
    // // Serial.println("==========================================================");
}

NESAPUEmulator::~NESAPUEmulator() {
    // CRITICAL: Set stopping flag FIRST to prevent ISR from running
    stopping_ = true;

    // Clear instance pointer so ISR won't access this object
    if (instance_ == this) {
        instance_ = nullptr;
    }

    // Give ISR time to finish if it's currently running
    delayMicroseconds(100);

    // Now stop the timer if it's still running
    // This is safe because stopping_ flag prevents ISR from doing work
    frameTimer_.end();

    // Free DMC sample data if allocated
    if (dmc_.vgmSampleData) {
        delete[] dmc_.vgmSampleData;
        dmc_.vgmSampleData = nullptr;
    }

    // Note: No // Serial.print here - destructor should be fast and safe
}

void NESAPUEmulator::stopFrameTimer() {
    // Set stopping flag first to prevent ISR from accessing object
    stopping_ = true;

    // Clear instance if it points to this object
    if (instance_ == this) {
        instance_ = nullptr;
    }

    // Small delay to let any in-flight ISR complete
    delayMicroseconds(200);

    // Now stop the timer (requires interrupts to be enabled!)
    // Note: IntervalTimer.end() is safe to call even if already stopped
    frameTimer_.end();
}

void NESAPUEmulator::startFrameTimer() {
    // Clear stopping flag to enable processing
    stopping_ = false;

    // Ensure instance pointer is set (for ISR)
    instance_ = this;

    // Start the frame counter timer at 240Hz (4166.67 microseconds)
    // Note: IntervalTimer.begin() replaces any existing timer
    frameTimer_.begin(frameCounterISR, 4167);
}

void NESAPUEmulator::reset() {
    memset(registers_, 0, sizeof(registers_));
    pulse1_.reset();
    pulse2_.reset();
    triangle_.reset();
    noise_.reset();

    // IMPORTANT: Set noise LFSR to 1 on power-up per NES APU spec
    // This prevents the LFSR from getting stuck at 0
    noise_.lfsr = 1;

    dmc_.reset();
    clockAccumulator_ = 0;
    cpuCycleEven_ = false;
    registerWriteCount_ = 0;
    updateCallCount_ = 0;
    nonZeroSampleCount_ = 0;
    lowpassFilterState_ = 0.0f;

    // Reset frame counter
    frameStep_ = 0;
    frameMode_ = false;  // 4-step mode
    frameIRQDisable_ = true;

    // Clear stopping flag in case we're reusing after stop
    stopping_ = false;

    // IMPORTANT: Do NOT start frame timer here!
    // The timer should only be started when playback begins (from VGMPlayer)
    // Starting it here can cause race conditions if called right after stop()

    // // Serial.println("NES APU Emulator: Reset (channels disabled until $4015 write)");
}

void NESAPUEmulator::writeRegister(uint8_t reg, uint8_t value) {
    registerWriteCount_++;

    // Map VGM register to NES APU register
    uint8_t nesReg = reg;
    if (reg >= 0x00 && reg <= 0x1F) {
        nesReg = reg;  // Direct mapping
    } else if (reg == 0x3F) {
        nesReg = 0x23;  // Special case
    } else {
        #if DEBUG_VGM_PLAYBACK
        // // Serial.print("NES APU: Invalid register $");
        // // Serial.println(reg, HEX);
        #endif
        return;
    }

    // Store in register array
    if (nesReg < 0x18) {
        registers_[nesReg] = value;
    }

    // Handle register writes
    switch (nesReg) {
        // Pulse 1: $4000-$4003
        case 0x00: {  // Duty, length halt, constant volume, volume
            pulse1_.dutyCycle = (value >> 6) & 0x03;
            pulse1_.lengthHalt = (value & 0x20) != 0;
            pulse1_.envelopeLoop = (value & 0x20) != 0;  // Same as lengthHalt
            bool wasConstantVolume = pulse1_.constantVolume;
            pulse1_.constantVolume = (value & 0x10) != 0;
            pulse1_.volume = value & 0x0F;
            pulse1_.envelopePeriod = value & 0x0F;  // V from bits 3-0

            // DEBUG: Log envelope mode changes
            #if DEBUG_VGM_PLAYBACK
            if (!wasConstantVolume && pulse1_.constantVolume) {
                // // Serial.println("NES APU: Pulse 1 switched to CONSTANT VOLUME mode");
            } else if (wasConstantVolume && !pulse1_.constantVolume) {
                // // Serial.print("NES APU: Pulse 1 switched to ENVELOPE mode (period=");
                // // Serial.print(pulse1_.envelopePeriod);
                // // Serial.println(")");
            }
            #endif
            break;
        }
        case 0x01: {  // Sweep unit - Phase 7
            pulse1_.sweepEnabled = (value & 0x80) != 0;
            pulse1_.sweepPeriod = (value >> 4) & 0x07;  // P from bits 6-4
            pulse1_.sweepNegate = (value & 0x08) != 0;
            pulse1_.sweepShift = value & 0x07;
            pulse1_.sweepReload = true;  // Set reload flag
            pulse1_.updateSweepMuting();  // Check muting immediately
            break;
        }
        case 0x02: {  // Timer low
            pulse1_.timerPeriod = (pulse1_.timerPeriod & 0x0700) | value;
            pulse1_.periodTooLow = (pulse1_.timerPeriod < 8);
            pulse1_.updateSweepMuting();  // Period changed, check sweep muting
            break;
        }
        case 0x03: {  // Length counter load, timer high
            pulse1_.timerPeriod = (pulse1_.timerPeriod & 0x00FF) | ((value & 0x07) << 8);
            pulse1_.periodTooLow = (pulse1_.timerPeriod < 8);
            pulse1_.updateSweepMuting();  // Period changed, check sweep muting

            // CRITICAL: Phase reset on $4003 write
            // Reset the duty cycle sequencer position (causes the "click")
            pulse1_.dutyPosition = 0;

            // IMPORTANT: The timer divider continues running!
            // However, if the timer is uninitialized (0), we need to set it to a valid state
            // to prevent underflow issues. Set to period+1 to match hardware behavior.
            if (pulse1_.timerCounter <= 0) {
                pulse1_.timerCounter = (float)(pulse1_.timerPeriod + 1);
            }

            // Phase 4: Load length counter (bits 7-3 = 5-bit index into lookup table)
            uint8_t lengthIndex = (value >> 3) & 0x1F;
            pulse1_.lengthCounter = lengthTable_[lengthIndex];

            // Phase 6: Set envelope start flag (from NESdev: "writing to $4003 sets the start flag")
            pulse1_.envelopeStart = true;

            // DEBUG: Log frequency calculation (commented for safety)
            // float frequency = 1789773.0f / (16.0f * (pulse1_.timerPeriod + 1));
            // // Serial.print("NES APU: Pulse 1 FREQ = ");
            // // Serial.print(frequency, 1);
            // // Serial.print(" Hz (timer=");
            // // Serial.print(pulse1_.timerPeriod);
            // // Serial.print(", length=");
            // // Serial.print(pulse1_.lengthCounter);
            // // Serial.println(")");
            break;
        }

        // Pulse 2: $4004-$4007
        case 0x04: {  // Duty, length halt, constant volume, volume
            pulse2_.dutyCycle = (value >> 6) & 0x03;
            pulse2_.lengthHalt = (value & 0x20) != 0;
            pulse2_.envelopeLoop = (value & 0x20) != 0;  // Same as lengthHalt
            pulse2_.constantVolume = (value & 0x10) != 0;
            pulse2_.volume = value & 0x0F;
            pulse2_.envelopePeriod = value & 0x0F;  // V from bits 3-0
            break;
        }
        case 0x05: {  // Sweep unit - Phase 7
            pulse2_.sweepEnabled = (value & 0x80) != 0;
            pulse2_.sweepPeriod = (value >> 4) & 0x07;  // P from bits 6-4
            pulse2_.sweepNegate = (value & 0x08) != 0;
            pulse2_.sweepShift = value & 0x07;
            pulse2_.sweepReload = true;  // Set reload flag
            pulse2_.updateSweepMuting();  // Check muting immediately
            break;
        }
        case 0x06: {  // Timer low
            pulse2_.timerPeriod = (pulse2_.timerPeriod & 0x0700) | value;
            pulse2_.periodTooLow = (pulse2_.timerPeriod < 8);
            pulse2_.updateSweepMuting();  // Period changed, check sweep muting
            break;
        }
        case 0x07: {  // Length counter load, timer high
            pulse2_.timerPeriod = (pulse2_.timerPeriod & 0x00FF) | ((value & 0x07) << 8);
            pulse2_.periodTooLow = (pulse2_.timerPeriod < 8);
            pulse2_.updateSweepMuting();  // Period changed, check sweep muting

            // CRITICAL: Phase reset on $4007 write
            // Reset the duty cycle sequencer position (causes the "click")
            pulse2_.dutyPosition = 0;

            // IMPORTANT: The timer divider continues running!
            // However, if the timer is uninitialized (0), we need to set it to a valid state
            if (pulse2_.timerCounter <= 0) {
                pulse2_.timerCounter = (float)(pulse2_.timerPeriod + 1);
            }

            // Phase 4: Load length counter (bits 7-3 = 5-bit index into lookup table)
            uint8_t lengthIndex = (value >> 3) & 0x1F;
            pulse2_.lengthCounter = lengthTable_[lengthIndex];

            // Phase 6: Set envelope start flag
            pulse2_.envelopeStart = true;

            // DEBUG: Log frequency calculation (commented for safety)
            // float frequency = 1789773.0f / (16.0f * (pulse2_.timerPeriod + 1));
            // // Serial.print("NES APU: Pulse 2 FREQ = ");
            // // Serial.print(frequency, 1);
            // // Serial.print(" Hz (timer=");
            // // Serial.print(pulse2_.timerPeriod);
            // // Serial.print(", length=");
            // // Serial.print(pulse2_.lengthCounter);
            // // Serial.println(")");
            break;
        }

        // Status: $4015
        case 0x15: {
            pulse1_.enabled = (value & 0x01) != 0;
            pulse2_.enabled = (value & 0x02) != 0;
            triangle_.enabled = (value & 0x04) != 0;
            noise_.enabled = (value & 0x08) != 0;
            dmc_.enabled = (value & 0x10) != 0;

            // Phase 4: CRITICAL QUIRK - Writing 0 to $4015 bit immediately clears length counter
            if (!pulse1_.enabled) {
                pulse1_.lengthCounter = 0;
            }
            if (!pulse2_.enabled) {
                pulse2_.lengthCounter = 0;
            }
            if (!triangle_.enabled) {
                triangle_.lengthCounter = 0;
            }
            if (!noise_.enabled) {
                noise_.lengthCounter = 0;
            }

            // DMC: If enabled and silent, restart sample
            if (dmc_.enabled && dmc_.silence) {
                // Check if we have valid sample data and parameters set
                if (dmc_.vgmSampleData && dmc_.vgmStartAddress >= 0xC000 && dmc_.vgmConfiguredLength > 0) {
                    #if DEBUG_VGM_PLAYBACK
                    // // Serial.println("NES APU: Starting DMC sample playback");
                    #endif
                    dmc_.startSample(dmc_.vgmStartAddress, dmc_.vgmConfiguredLength);
                }
            } else if (!dmc_.enabled) {
                // If disabled, silence immediately but keep output level
                dmc_.silence = true;
                // Don't clear sampleLength here - keep it for restart
            }

            // Log $4015 writes (critical for debugging - but VERY chatty!)
            #if DEBUG_VGM_PLAYBACK
            // // Serial.print("NES APU: $4015 (Status) = $");
            // // Serial.print(value, HEX);
            // // Serial.print(" | P1=");
            // // Serial.print(pulse1_.enabled ? "ON" : "OFF");
            // // Serial.print(" P2=");
            // // Serial.print(pulse2_.enabled ? "ON" : "OFF");
            // // Serial.print(" T=");
            // // Serial.print(triangle_.enabled ? "ON" : "OFF");
            // // Serial.print(" N=");
            // // Serial.print(noise_.enabled ? "ON" : "OFF");
            // // Serial.print(" D=");
            // // Serial.println(dmc_.enabled ? "ON" : "OFF");
            #endif
            break;
        }

        // Triangle: $4008-$400B
        case 0x08: {  // Linear counter control, length halt
            triangle_.linearControl = (value & 0x80) != 0;
            triangle_.lengthHalt = (value & 0x80) != 0;  // Same bit as control
            triangle_.linearReload = value & 0x7F;  // 7-bit reload value
            break;
        }
        case 0x09: {  // Unused
            break;
        }
        case 0x0A: {  // Timer low
            triangle_.timerPeriod = (triangle_.timerPeriod & 0x0700) | value;
            triangle_.periodTooLow = (triangle_.timerPeriod < 2);  // Ultrasonic silencing
            break;
        }
        case 0x0B: {  // Length counter load, timer high
            triangle_.timerPeriod = (triangle_.timerPeriod & 0x00FF) | ((value & 0x07) << 8);
            triangle_.periodTooLow = (triangle_.timerPeriod < 2);  // Ultrasonic silencing

            // Load length counter (bits 7-3 = 5-bit index into lookup table)
            uint8_t lengthIndex = (value >> 3) & 0x1F;
            triangle_.lengthCounter = lengthTable_[lengthIndex];

            // Set linear counter reload flag
            triangle_.linearReloadFlag = true;

            // DEBUG: Log frequency calculation (commented for safety)
            // float frequency = 1789773.0f / (32.0f * (triangle_.timerPeriod + 1));
            // // Serial.print("NES APU: Triangle FREQ = ");
            // // Serial.print(frequency, 1);
            // // Serial.println(" Hz");
            break;
        }

        // Noise: $400C-$400F
        case 0x0C: {  // Length halt, constant volume, volume/envelope
            noise_.lengthHalt = (value & 0x20) != 0;
            noise_.envelopeLoop = (value & 0x20) != 0;  // Same as lengthHalt
            noise_.constantVolume = (value & 0x10) != 0;
            noise_.volume = value & 0x0F;
            noise_.envelopePeriod = value & 0x0F;  // Envelope period
            break;
        }
        case 0x0D: {  // Unused
            break;
        }
        case 0x0E: {  // Mode and period
            noise_.mode = (value & 0x80) != 0;  // Bit 7: 0 = normal, 1 = short
            noise_.periodIndex = value & 0x0F;  // Bits 3-0: period index
            break;
        }
        case 0x0F: {  // Length counter load
            // Load length counter (bits 7-3 = 5-bit index into lookup table)
            uint8_t lengthIndex = (value >> 3) & 0x1F;
            noise_.lengthCounter = lengthTable_[lengthIndex];

            // Set envelope start flag
            noise_.envelopeStart = true;

            // DEBUG: Log mode and period
            // // Serial.print("NES APU: Noise mode=");
            // // Serial.print(noise_.mode ? "SHORT" : "NORMAL");
            // // Serial.print(" period=");
            // // Serial.println(noisePeriodTable_[noise_.periodIndex]);
            break;
        }

        // DMC: $4010-$4013
        case 0x10: {  // Flags and rate
            dmc_.irqEnabled = (value & 0x80) != 0;
            dmc_.loop = (value & 0x40) != 0;
            dmc_.rateIndex = value & 0x0F;

            // If IRQ flag is cleared, clear IRQ (not used in VGM)
            // if (!dmc_.irqEnabled) { /* clear IRQ */ }
            break;
        }
        case 0x11: {  // Direct load (7-bit DAC)
            dmc_.outputLevel = value & 0x7F;

            // DEBUG: Log direct load
            // // Serial.print("NES APU: DMC direct load = ");
            // // Serial.println(dmc_.outputLevel);
            break;
        }
        case 0x12: {  // Sample address
            // Sample address = $C000 + (A * 64)
            uint16_t addr = 0xC000 + ((value & 0xFF) * 64);
            dmc_.vgmStartAddress = addr;

            #if DEBUG_VGM_PLAYBACK
            // // Serial.print("NES APU: DMC sample address = $");
            // // Serial.println(addr, HEX);
            #endif
            break;
        }
        case 0x13: {  // Sample length
            // Sample length = (L * 16) + 1
            uint16_t length = ((value & 0xFF) * 16) + 1;

            // Store the configured length (preserved for restart via $4015)
            dmc_.vgmConfiguredLength = length;

            // If DMC is enabled and not playing, start sample
            if (dmc_.enabled && dmc_.silence) {
                dmc_.startSample(dmc_.vgmStartAddress, length);
            }

            #if DEBUG_VGM_PLAYBACK
            // // Serial.print("NES APU: DMC sample length = ");
            // // Serial.println(length);
            #endif
            break;
        }

        // Frame counter: $4017
        case 0x17: {
            frameMode_ = (value & 0x80) != 0;  // Bit 7: 0 = 4-step, 1 = 5-step
            frameIRQDisable_ = (value & 0x40) != 0;  // Bit 6: IRQ inhibit

            // Reset frame step
            frameStep_ = 0;

            // CRITICAL QUIRK: If mode = 1 (5-step), immediately clock length and envelope
            if (frameMode_) {
                // 5-step mode: Immediately clock quarter-frame and half-frame units
                // This approximates the 3-4 CPU cycle delay as "immediate"

                // Clock linear counter (quarter-frame)
                triangle_.clockLinearCounter();

                // Clock envelopes (quarter-frame)
                pulse1_.clockEnvelope();
                pulse2_.clockEnvelope();
                noise_.clockEnvelope();

                // Clock length counters (half-frame)
                pulse1_.clockLength();
                pulse2_.clockLength();
                triangle_.clockLength();
                noise_.clockLength();

                // Clock sweep units (half-frame)
                pulse1_.clockSweep();
                pulse2_.clockSweep();

                #if DEBUG_VGM_PLAYBACK
                // // Serial.println("NES APU: Frame counter switched to 5-step mode (immediate clocks applied)");
                #endif
            } else {
                #if DEBUG_VGM_PLAYBACK
                // // Serial.println("NES APU: Frame counter switched to 4-step mode");
                #endif
            }

            break;
        }
    }

    // Debug logging (every 100th write)
    #if DEBUG_VGM_PLAYBACK
    if (registerWriteCount_ % 100 == 0) {
        // // Serial.print("NES APU: $");
        // // Serial.print(0x4000 + nesReg, HEX);
        // // Serial.print(" (");
        // // Serial.print(getRegisterName(reg));
        // // Serial.print(") = $");
        // // Serial.print(value, HEX);
        // // Serial.print(" [");
        // // Serial.print(registerWriteCount_);
        // // Serial.println(" total]");
    }
    #endif
}

void NESAPUEmulator::loadDPCMData(const uint8_t* data, size_t size) {
    // Legacy method - loads at start of buffer
    ensureDPCMBuffer();
    loadDPCMDataAtOffset(data, size, 0);
}

void NESAPUEmulator::ensureDPCMBuffer() {
    // Ensure we have the full 16KB DPCM buffer allocated
    if (!dmc_.vgmSampleData) {
        dmc_.vgmSampleData = new uint8_t[16384];
        memset(dmc_.vgmSampleData, 0x55, 16384);  // Fill with 0x55 (silence pattern)
        dmc_.vgmSampleSize = 16384;

        // Set sample data pointer for when samples start
        dmc_.sampleData = dmc_.vgmSampleData;

        // CRITICAL: Ensure DMC is truly silent during loading
        dmc_.silence = true;  // Stay silent until a sample is started
        dmc_.bitsRemaining = 0;  // No bits to process
        dmc_.sampleLength = 0;  // No sample playing

        // // Serial.println("NES APU: Allocated 16KB DPCM buffer");
    }
}

void NESAPUEmulator::loadDPCMDataAtOffset(const uint8_t* data, size_t size, uint16_t offset) {
    // Ensure buffer exists
    ensureDPCMBuffer();

    // Validate parameters
    if (!data || size == 0) return;
    if (offset >= 16384) {
        // // Serial.print("NES APU: Invalid DPCM offset ");
        // // Serial.println(offset);
        return;
    }

    // Clamp size to available space
    size_t actualSize = size;
    if (offset + size > 16384) {
        actualSize = 16384 - offset;
        // // Serial.print("NES APU: Clamping DPCM data from ");
        // // Serial.print(size);
        // // Serial.print(" to ");
        // // Serial.print(actualSize);
        // // Serial.println(" bytes");
    }

    // Copy data to the specified offset
    memcpy(dmc_.vgmSampleData + offset, data, actualSize);

    // // Serial.print("NES APU: Loaded ");
    // // Serial.print(actualSize);
    // // Serial.print(" bytes at offset ");
    // // Serial.print(offset);
    // // Serial.print(" ($");
    // // Serial.print(0xC000 + offset, HEX);
    // // Serial.println(")");
}

// Pulse channel timer clocking
void NESAPUEmulator::PulseChannel::clockTimer() {
    // HARDWARE ACCURACY: Timer ALWAYS runs, even when period is 0
    // This is important for proper phase behavior

    // Decrement timer (counting down APU clocks)
    timerCounter -= 1.0f;

    // Timer expired?
    if (timerCounter <= 0) {
        // CRITICAL: NES APU pulse channel timing:
        // When the timer reaches 0, it reloads with the period value
        // and clocks the duty cycle sequencer.
        // The actual period is (t+1) APU cycles because it counts:
        // t, t-1, t-2, ..., 1, 0, then reloads to t
        // This gives us t+1 states total

        // Add the period+1 to handle any fractional underflow
        // This maintains sub-sample accuracy
        timerCounter += (float)(timerPeriod + 1);

        // CRITICAL: Sequencer counts DOWN (reads positions 0, 7, 6, 5, 4, 3, 2, 1)
        // Only update duty position if period is valid (prevents weird behavior at period=0)
        if (timerPeriod > 0) {
            dutyPosition = (dutyPosition - 1) & 0x07;
        }
    }
}

// Get raw waveform state (0 or 1) - for band-limited synthesis
uint8_t NESAPUEmulator::PulseChannel::getRawWaveform() {
    // Silence conditions (in order of likelihood for performance)
    if (!enabled) return 0;
    if (lengthCounter == 0) return 0;  // Phase 4: Length counter reached 0 = silence
    if (periodTooLow) return 0;  // Period < 8 silences channel (NES hardware quirk)
    if (sweepMuting) return 0;   // Phase 7: Sweep muting (target period > $7FF)

    // ADDITIONAL CHECK: Period of 0 also silences (hardware behavior)
    if (timerPeriod == 0) return 0;

    // Return current duty cycle value (0 or 1)
    return NESAPUEmulator::dutySequences_[dutyCycle][dutyPosition];
}

// Get pulse channel output (0-15) - volume-scaled
uint8_t NESAPUEmulator::PulseChannel::getOutput() {
    // Get raw waveform
    uint8_t waveform = getRawWaveform();

    // If waveform is 0, output is 0 regardless of volume
    if (waveform == 0) return 0;

    // Phase 6: Choose volume source (constant volume or envelope decay)
    if (constantVolume) {
        return volume;  // Use constant volume parameter (0-15)
    } else {
        return envelopeDecay;  // Use envelope decay level (0-15)
    }
}

// Clock length counter (Phase 4) - called at 120Hz from frame counter
void NESAPUEmulator::PulseChannel::clockLength() {
    // Only decrement if not halted and counter > 0
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

// Clock envelope (Phase 6) - called at 240Hz from frame counter ISR
// CRITICAL: This is called from an interrupt, so NO // Serial.print allowed!
void NESAPUEmulator::PulseChannel::clockEnvelope() {
    if (envelopeStart) {
        // Start flag set - reset envelope
        envelopeStart = false;
        envelopeDecay = 15;  // Reset decay level to max
        envelopeDivider = envelopePeriod;  // Reload divider with period (n, not n+1 here)
    } else {
        // Clock divider - operates at period of (n+1)
        if (envelopeDivider > 0) {
            envelopeDivider--;
        } else {
            // Divider expired - reload with period value
            // This gives us period of n+1: counts n, n-1, ..., 1, 0, then reloads
            envelopeDivider = envelopePeriod;

            // Clock the decay counter
            if (envelopeDecay > 0) {
                envelopeDecay--;  // Decay down to 0
            } else if (envelopeLoop) {
                // Decay at 0 with loop flag - reload to 15
                envelopeDecay = 15;
            }
            // If decay at 0 without loop, stays at 0 (silence)
        }
    }
}

// Clock sweep unit (Phase 7) - called at 120Hz from frame counter
void NESAPUEmulator::PulseChannel::clockSweep() {
    // Check reload flag
    if (sweepReload) {
        // Reload divider (immediately on next clock, not this one)
        sweepDivider = sweepPeriod;
        sweepReload = false;
        return;  // Reload doesn't clock the sweep
    }

    // Clock divider
    if (sweepDivider > 0) {
        sweepDivider--;
    } else {
        // Divider reached 0 - reload and update period
        sweepDivider = sweepPeriod;

        // Only update period if enabled, shift > 0, and not muting
        if (sweepEnabled && sweepShift > 0 && !sweepMuting) {
            uint16_t targetPeriod = calculateSweepTarget();

            // CRITICAL: Only update period if target is valid
            // The hardware won't update if target would cause muting
            if (targetPeriod >= 8 && targetPeriod <= 0x7FF) {
                timerPeriod = targetPeriod;
                // Re-check muting after period update
                updateSweepMuting();
            }
        }
    }
}

// Calculate sweep target period (Phase 7)
uint16_t NESAPUEmulator::PulseChannel::calculateSweepTarget() {
    // Barrel shifter: period >> shift
    uint16_t delta = timerPeriod >> sweepShift;

    if (sweepNegate) {
        // Negate mode (pitch decreases)
        // CRITICAL: Check for underflow before doing subtraction!
        if (sweepOnesComplement) {
            // Pulse 1 uses ones' complement (subtract delta + 1)
            if (timerPeriod >= delta + 1) {
                return timerPeriod - delta - 1;
            } else {
                // Would underflow - return 0 (will trigger muting)
                return 0;
            }
        } else {
            // Pulse 2 uses two's complement (subtract delta)
            if (timerPeriod >= delta) {
                return timerPeriod - delta;
            } else {
                // Would underflow - return 0 (will trigger muting)
                return 0;
            }
        }
    } else {
        // Add mode (pitch increases)
        uint32_t result = (uint32_t)timerPeriod + delta;
        // Clamp to 16-bit max (overflow will be caught by muting check)
        if (result > 0xFFFF) {
            return 0xFFFF;
        }
        return (uint16_t)result;
    }
}

// Update sweep muting state (Phase 7)
void NESAPUEmulator::PulseChannel::updateSweepMuting() {
    // Two muting conditions:
    // 1. Period < 8 (period too low)
    periodTooLow = (timerPeriod < 8);

    // 2. Target period > $7FF (sweep overflow)
    // NOTE: Sweep muting only applies when shift > 0
    if (sweepShift > 0) {
        uint16_t targetPeriod = calculateSweepTarget();
        sweepMuting = (targetPeriod > 0x7FF);
    } else {
        sweepMuting = false;  // No sweep = no sweep muting
    }
}

// Triangle channel timer clocking (Phase 8)
void NESAPUEmulator::TriangleChannel::clockTimer() {
    // Triangle timer runs at CPU rate, not APU rate (twice as fast as pulse)
    // No period check - triangle always runs

    // Decrement timer (counting down CPU clocks)
    timerCounter -= 1.0f;

    // Timer expired?
    if (timerCounter <= 0) {
        // Reload with period+1
        timerCounter += (float)(timerPeriod + 1);

        // CRITICAL: Triangle sequencer increments through 32 steps
        // Only update if both length counter and linear counter are non-zero
        if (lengthCounter > 0 && linearCounter > 0) {
            sequenceStep = (sequenceStep + 1) & 0x1F;  // Wrap at 32
        }
    }
}

// Get triangle channel output (Phase 8)
uint8_t NESAPUEmulator::TriangleChannel::getOutput() {
    // Silence conditions
    if (!enabled) return 0;

    // Ultrasonic frequencies (period < 2) also silence to prevent aliasing
    // But the sequencer continues running (phase continuity)
    if (periodTooLow) return 0;

    // IMPORTANT: Do NOT gate to 0 when length/linear counters are 0
    // The DAC holds the last value on real hardware to prevent pops/clicks
    // The clocking already halts the sequencer when counters are zero,
    // so the output level will naturally hold

    // Return current sequence value (0-15)
    // Triangle has no volume control!
    return NESAPUEmulator::triangleSequence_[sequenceStep];
}

// Clock triangle linear counter (Phase 8) - called at 240Hz
void NESAPUEmulator::TriangleChannel::clockLinearCounter() {
    if (linearReloadFlag) {
        // Reload linear counter
        linearCounter = linearReload;
    } else if (linearCounter > 0) {
        // Decrement counter
        linearCounter--;
    }

    // Clear reload flag if control bit is clear
    if (!linearControl) {
        linearReloadFlag = false;
    }
}

// Clock triangle length counter (Phase 8) - called at 120Hz
void NESAPUEmulator::TriangleChannel::clockLength() {
    // Only decrement if not halted and counter > 0
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

// ============================================================================
// NOISE CHANNEL IMPLEMENTATION (Phase 9)
// ============================================================================

// Clock the noise timer
void NESAPUEmulator::NoiseChannel::clockTimer() {
    // Noise timer runs at APU rate (every other CPU cycle, like pulse)
    // IMPORTANT: Period table is in CPU cycles, not APU cycles
    // Since we clock at APU rate (every 2 CPU cycles), we must decrement by 2
    timerCounter -= 2.0f;  // Was 1.0f - this fixes the 2x pitch error

    // Timer expired?
    if (timerCounter <= 0) {
        // Reload timer from period table (values are in CPU cycles)
        timerCounter += (float)NESAPUEmulator::noisePeriodTable_[periodIndex];

        // Shift the LFSR
        shiftLFSR();
    }
}

// Shift the Linear Feedback Shift Register
void NESAPUEmulator::NoiseChannel::shiftLFSR() {
    // Get feedback bits
    uint8_t bit0 = lfsr & 1;
    uint8_t bit1 = mode ? ((lfsr >> 6) & 1) : ((lfsr >> 1) & 1);

    // Calculate feedback (XOR)
    uint8_t feedback = bit0 ^ bit1;

    // Shift right and insert feedback at bit 14
    lfsr = (lfsr >> 1) | (feedback << 14);
}

// Get noise channel output
uint8_t NESAPUEmulator::NoiseChannel::getOutput() {
    // Silence conditions
    if (!enabled) return 0;
    if (lengthCounter == 0) return 0;

    // Check LFSR output bit (bit 0)
    // When bit 0 is set, the noise channel is silenced
    if ((lfsr & 1) != 0) return 0;

    // Return volume (either constant or envelope)
    if (constantVolume) {
        return volume & 0x0F;
    } else {
        return envelopeDecay & 0x0F;
    }
}

// Clock the noise envelope (240Hz)
void NESAPUEmulator::NoiseChannel::clockEnvelope() {
    if (envelopeStart) {
        // Start flag was set - restart envelope
        envelopeDecay = 15;
        envelopeDivider = envelopePeriod;
        envelopeStart = false;
    } else {
        // Clock the divider
        if (envelopeDivider > 0) {
            envelopeDivider--;
        } else {
            // Divider expired, reload and clock decay
            envelopeDivider = envelopePeriod;

            if (envelopeDecay > 0) {
                envelopeDecay--;
            } else if (envelopeLoop) {
                // Loop back to 15
                envelopeDecay = 15;
            }
        }
    }
}

// Clock the noise length counter (120Hz)
void NESAPUEmulator::NoiseChannel::clockLength() {
    // Only decrement if not halted and counter > 0
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

// ============================================================================
// DMC CHANNEL IMPLEMENTATION (Phase 10)
// ============================================================================

// Clock the DMC timer
void NESAPUEmulator::DMCChannel::clockTimer() {
    // DMC timer runs at CPU rate with its own divider
    if (silence) return;  // Don't clock if silent

    timerCounter -= 1.0f;

    // Timer expired?
    if (timerCounter <= 0) {
        // Reload timer from rate table
        timerCounter += (float)NESAPUEmulator::dmcRateTable_[rateIndex];

        // Process next bit
        processNextBit();
    }
}

// Process the next bit from the sample
void NESAPUEmulator::DMCChannel::processNextBit() {
    // If we need a new byte
    if (bitsRemaining == 0) {
        // Check if we have more sample data
        if (sampleLength > 0) {
            // Load next byte
            if (sampleData && sampleAddress < vgmSampleSize) {
                sampleBuffer = sampleData[sampleAddress];
                sampleAddress++;
                sampleLength--;
                bitsRemaining = 8;
            } else {
                // Out of data
                sampleLength = 0;
            }
        }

        // If we're out of data
        if (sampleLength == 0) {
            if (loop) {
                // Restart sample from beginning using configured length
                startSample(vgmStartAddress, vgmConfiguredLength);
            } else {
                // Sample finished
                silence = true;
                // IRQ would fire here if enabled (not used in VGM)
            }
            return;
        }
    }

    // Process current bit (if we have one)
    if (bitsRemaining > 0) {
        // Check bit 0 of sample buffer
        if (sampleBuffer & 1) {
            // Bit = 1: Increment output level by 2
            if (outputLevel <= 125) {
                outputLevel += 2;
            }
        } else {
            // Bit = 0: Decrement output level by 2
            if (outputLevel >= 2) {
                outputLevel -= 2;
            }
        }

        // Shift to next bit
        sampleBuffer >>= 1;
        bitsRemaining--;
    }
}

// Start sample playback
void NESAPUEmulator::DMCChannel::startSample(uint16_t address, uint16_t length) {
    #if DEBUG_VGM_PLAYBACK
    // // Serial.print("DMC: startSample addr=$");
    // // Serial.print(address, HEX);
    // // Serial.print(" len=");
    // // Serial.print(length);
    // // Serial.print(" vgmData=");
    // // Serial.print((uint32_t)vgmSampleData, HEX);
    // // Serial.print(" vgmSize=");
    // // Serial.println(vgmSampleSize);
    #endif

    // Calculate actual address in VGM sample data
    // VGM uses the standard NES memory map: $C000-$FFFF
    if (address >= 0xC000 && vgmSampleData) {
        uint16_t offset = address - 0xC000;
        if (offset < vgmSampleSize && length > 0) {
            sampleAddress = offset;
            sampleLength = (length > vgmSampleSize - offset) ? (vgmSampleSize - offset) : length;
            silence = false;
            bitsRemaining = 0;  // Force fetch of first byte
            #if DEBUG_VGM_PLAYBACK
            // // Serial.print("DMC: Starting at offset ");
            // // Serial.print(offset);
            // // Serial.print(" for ");
            // // Serial.print(sampleLength);
            // // Serial.println(" bytes");
            #endif
        } else {
            #if DEBUG_VGM_PLAYBACK
            // // Serial.print("DMC: Invalid offset ");
            // // Serial.print(offset);
            // // Serial.print(" or length ");
            // // Serial.println(length);
            #endif
        }
    } else {
        #if DEBUG_VGM_PLAYBACK
        // // Serial.println("DMC: No sample data loaded or invalid address");
        #endif
    }
}

// Get DMC output
uint8_t NESAPUEmulator::DMCChannel::getOutput() {
    // DMC always outputs its level (even when silent)
    // The 7-bit counter IS the output
    return outputLevel & 0x7F;
}

// Nonlinear mixing formula from NESdev wiki
// Using the PROPER LOOKUP TABLE approach from accurate emulators
float NESAPUEmulator::mixChannels(uint8_t pulse1Out, uint8_t pulse2Out, uint8_t triangleOut, uint8_t noiseOut, uint8_t dmcOut) {
    // NES APU nonlinear mixing - LOOKUP TABLE APPROACH
    // From NESdev wiki and verified against Mesen/FCEUX/Nestopia source

    // Pulse lookup table formula: pulse_table[n] = 95.52 / (8128.0 / n + 100)
    float pulseOut = 0.0f;
    uint8_t pulseIndex = pulse1Out + pulse2Out;
    if (pulseIndex > 0) {
        pulseOut = 95.52f / (8128.0f / (float)pulseIndex + 100.0f);
    }

    // TND lookup table formula: tnd_table[n] = 163.67 / (24329.0 / n + 100)
    // Index = 3 * triangle + 2 * noise + dmc
    float tndOut = 0.0f;
    uint16_t tndIndex = 3 * triangleOut + 2 * noiseOut + dmcOut;  // DMC is 0-127
    if (tndIndex > 0) {
        tndOut = 163.67f / (24329.0f / (float)tndIndex + 100.0f);
    }

    // Combine outputs
    float combined = pulseOut + tndOut;

    // Actual output levels with proper lookup tables:
    // pulse_table[15] (single pulse): 95.52 / (641.87) = 0.1488
    // pulse_table[30] (both pulses): 95.52 / (370.93) = 0.2575
    // tnd_table[45] (triangle*3): 163.67 / (640.64) = 0.2554
    // tnd_table[202] (max with DMC): 163.67 / (220.44) = 0.742

    // The triangle IS loud when using lookup table - this is CORRECT!
    // The *3 multiplier is part of the accurate emulation

    // Scale to audio range - but prevent clipping!
    // Max theoretical output is ~1.0, so scale conservatively
    return combined * 1.0f;  // Reduced from 1.5 to prevent distortion
}

// Stereo nonlinear mixing with panning
// Pans pulse1 left (70/30), pulse2 right (30/70), noise by frequency
// Triangle and DMC remain centered
void NESAPUEmulator::mixChannelsStereo(
    uint8_t pulse1Out, uint8_t pulse2Out,
    uint8_t triangleOut, uint8_t noiseOut, uint8_t dmcOut,
    uint8_t noisePeriodIndex,
    float& outLeft, float& outRight
) {
    // Stereo loudness compensation factor
    // Compensates for the loudness loss from panning (0.7/0.3 split)
    // This makes stereo mode as loud as mono mode
    static constexpr float STEREO_PULSE_BOOST = 1.4f;

    // Apply stereo compensation boost to pulse channels
    float pulse1Boosted = pulse1Out * STEREO_PULSE_BOOST;
    float pulse2Boosted = pulse2Out * STEREO_PULSE_BOOST;

    // Calculate noise panning based on frequency (period index 0-15)
    // High frequency (low period index = 0) → pan left (70/30)
    // Low frequency (high period index = 15) → pan right (30/70)
    // Linear interpolation between the two
    float noisePanLeft = 0.7f - (noisePeriodIndex / 15.0f) * 0.4f;   // 0.7 → 0.3
    float noisePanRight = 0.3f + (noisePeriodIndex / 15.0f) * 0.4f;  // 0.3 → 0.7

    // LEFT CHANNEL
    // Apply pan coefficients to boosted pulse channels
    float pulse1Left = pulse1Boosted * 0.7f;   // Pulse 1 mostly left
    float pulse2Left = pulse2Boosted * 0.3f;   // Pulse 2 slightly left
    float pulseSumLeft = pulse1Left + pulse2Left;

    // Apply nonlinear pulse mixing
    float pulseOutLeft = 0.0f;
    if (pulseSumLeft > 0.0f) {
        pulseOutLeft = 95.52f / (8128.0f / pulseSumLeft + 100.0f);
    }

    // Apply noise panning before TND mixing
    float noiseLeft = noiseOut * noisePanLeft;

    // TND mixing (triangle and DMC centered)
    float tndIndexLeft = 3.0f * triangleOut + 2.0f * noiseLeft + dmcOut;
    float tndOutLeft = 0.0f;
    if (tndIndexLeft > 0.0f) {
        tndOutLeft = 163.67f / (24329.0f / tndIndexLeft + 100.0f);
    }

    outLeft = pulseOutLeft + tndOutLeft;

    // RIGHT CHANNEL
    // Apply pan coefficients to boosted pulse channels
    float pulse1Right = pulse1Boosted * 0.3f;  // Pulse 1 slightly right
    float pulse2Right = pulse2Boosted * 0.7f;  // Pulse 2 mostly right
    float pulseSumRight = pulse1Right + pulse2Right;

    // Apply nonlinear pulse mixing
    float pulseOutRight = 0.0f;
    if (pulseSumRight > 0.0f) {
        pulseOutRight = 95.52f / (8128.0f / pulseSumRight + 100.0f);
    }

    // Apply noise panning before TND mixing
    float noiseRight = noiseOut * noisePanRight;

    // TND mixing (triangle and DMC centered)
    float tndIndexRight = 3.0f * triangleOut + 2.0f * noiseRight + dmcOut;
    float tndOutRight = 0.0f;
    if (tndIndexRight > 0.0f) {
        tndOutRight = 163.67f / (24329.0f / tndIndexRight + 100.0f);
    }

    outRight = pulseOutRight + tndOutRight;
}

// Apply NES analog output filters (90Hz HPF, 440Hz HPF, 14kHz LPF)
// These simulate the analog path in the NES and remove DC offset/aliasing
inline float NESAPUEmulator::applyOutputFilters(float x) {
    // HPF 90 Hz - removes DC offset and very low frequency content
    float y1 = x - hpf90_x1_ + hpf90_a_ * hpf90_y1_;
    hpf90_x1_ = x;
    hpf90_y1_ = y1;

    // HPF 440 Hz - further high-pass filtering characteristic of NES
    float y2 = y1 - hpf440_x1_ + hpf440_a_ * hpf440_y1_;
    hpf440_x1_ = y1;
    hpf440_y1_ = y2;

    // LPF 14 kHz - removes high frequency aliasing
    float y3 = lpf14k_y1_ + lpf14k_a_ * (y2 - lpf14k_y1_);
    lpf14k_y1_ = y3;

    return y3;
}

// Apply NES analog output filters to LEFT channel (stereo mode)
inline float NESAPUEmulator::applyOutputFiltersLeft(float x) {
    // HPF 90 Hz - removes DC offset and very low frequency content
    float y1 = x - hpf90_x1_left_ + hpf90_a_ * hpf90_y1_left_;
    hpf90_x1_left_ = x;
    hpf90_y1_left_ = y1;

    // HPF 440 Hz - further high-pass filtering characteristic of NES
    float y2 = y1 - hpf440_x1_left_ + hpf440_a_ * hpf440_y1_left_;
    hpf440_x1_left_ = y1;
    hpf440_y1_left_ = y2;

    // LPF 14 kHz - removes high frequency aliasing
    float y3 = lpf14k_y1_left_ + lpf14k_a_ * (y2 - lpf14k_y1_left_);
    lpf14k_y1_left_ = y3;

    return y3;
}

// Apply NES analog output filters to RIGHT channel (stereo mode)
inline float NESAPUEmulator::applyOutputFiltersRight(float x) {
    // HPF 90 Hz - removes DC offset and very low frequency content
    float y1 = x - hpf90_x1_right_ + hpf90_a_ * hpf90_y1_right_;
    hpf90_x1_right_ = x;
    hpf90_y1_right_ = y1;

    // HPF 440 Hz - further high-pass filtering characteristic of NES
    float y2 = y1 - hpf440_x1_right_ + hpf440_a_ * hpf440_y1_right_;
    hpf440_x1_right_ = y1;
    hpf440_y1_right_ = y2;

    // LPF 14 kHz - removes high frequency aliasing
    float y3 = lpf14k_y1_right_ + lpf14k_a_ * (y2 - lpf14k_y1_right_);
    lpf14k_y1_right_ = y3;

    return y3;
}

// AudioStream update method - called at 44.1kHz by Teensy Audio Library ISR
void NESAPUEmulator::update() {
    // Check stopping flag IMMEDIATELY
    if (stopping_) {
        // CRITICAL: NO // Serial.print in Audio ISR!
        return;
    }

    // Allocate output buffers FIRST (required even if stopping)
    audio_block_t* blockLeft = allocate();
    audio_block_t* blockRight = allocate();

    if (!blockLeft || !blockRight) {
        // Out of memory - release what we have and return
        if (blockLeft) release(blockLeft);
        if (blockRight) release(blockRight);
        return;
    }

    updateCallCount_++;

    // REMOVED: Serial logging from ISR context
    // Printing from the audio interrupt can cause system instability/reboots
    // Log data can be collected and printed from main loop if needed

    /* Commented out to prevent ISR issues:
    if (updateCallCount_ % 1000 == 0) {
        // // Serial.print("NES APU: P1[");
        // // Serial.print(pulse1_.enabled ? "ON" : "OFF");
        // // Serial.print(" ");
        if (pulse1_.constantVolume) {
            // // Serial.print("CV:");
            // // Serial.print(pulse1_.volume);
        } else {
            // // Serial.print("ENV:");
            // // Serial.print(pulse1_.envelopeDecay);
            // // Serial.print("/");
            // // Serial.print(pulse1_.envelopePeriod);
            // // Serial.print(pulse1_.envelopeLoop ? "L" : "");
        }
        // // Serial.print(" T:");
        // // Serial.print(pulse1_.timerPeriod);
        // // Serial.print("] P2[");
        // // Serial.print(pulse2_.enabled ? "ON" : "OFF");
        // // Serial.print(" ");
        if (pulse2_.constantVolume) {
            // // Serial.print("CV:");
            // // Serial.print(pulse2_.volume);
        } else {
            // // Serial.print("ENV:");
            // // Serial.print(pulse2_.envelopeDecay);
            // // Serial.print("/");
            // // Serial.print(pulse2_.envelopePeriod);
            // // Serial.print(pulse2_.envelopeLoop ? "L" : "");
        }
        // // Serial.print(" T:");
        // // Serial.print(pulse2_.timerPeriod);
        // // Serial.print("] Samples:");
        // // Serial.println(nonZeroSampleCount_);
    }
    */

    // Generate AUDIO_BLOCK_SAMPLES (128) samples (blocks already allocated above)
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        // SIMPLIFIED CORRECT IMPLEMENTATION
        // The nonlinear mixer expects the direct channel outputs (0-15)
        // NOT band-limited averaged values!

        // Clock APU at 1.789773 MHz (accumulated per sample)
        // We need to clock ~40.58 CPU cycles per sample
        clockAccumulator_ += CPU_CLOCKS_PER_SAMPLE;

        // CRITICAL: Different channels clock at different rates!
        while (clockAccumulator_ >= 1.0f) {
            // Triangle clocks EVERY CPU cycle (CPU rate)
            triangle_.clockTimer();

            // DMC also clocks EVERY CPU cycle (but uses its own divider)
            dmc_.clockTimer();

            // Pulse and noise channels clock on EVERY OTHER CPU cycle (APU rate)
            if (cpuCycleEven_) {
                pulse1_.clockTimer();
                pulse2_.clockTimer();
                noise_.clockTimer();
            }
            cpuCycleEven_ = !cpuCycleEven_;
            clockAccumulator_ -= 1.0f;
        }

        // Get the CURRENT output of each channel (this is what the hardware does)
        // The nonlinear mixer works on the instantaneous channel outputs
        uint8_t pulse1Out = pulse1_.getOutput();  // Returns 0-15 (volume-scaled)
        uint8_t pulse2Out = pulse2_.getOutput();  // Returns 0-15 (volume-scaled)
        uint8_t triangleOut = triangle_.getOutput();  // Returns 0-15 (no volume control)
        uint8_t noiseOut = noise_.getOutput();  // Returns 0-15 (volume-scaled)
        uint8_t dmcOut = dmc_.getOutput();  // Returns 0-127 (7-bit DAC)

        // Access global settings
        extern bool g_nesFiltersEnabled;
        extern bool g_nesStereoEnabled;

        float outputLeft, outputRight;

        if (g_nesStereoEnabled) {
            // STEREO MODE: Use stereo mixer with panning
            mixChannelsStereo(pulse1Out, pulse2Out, triangleOut, noiseOut, dmcOut,
                            noise_.periodIndex, outputLeft, outputRight);

            // Apply filters separately to each channel if enabled
            if (g_nesFiltersEnabled) {
                outputLeft = applyOutputFiltersLeft(outputLeft);
                outputRight = applyOutputFiltersRight(outputRight);
            }
        } else {
            // MONO MODE: Use original mono mixer (unchanged behavior)
            float mixed = mixChannels(pulse1Out, pulse2Out, triangleOut, noiseOut, dmcOut);

            // Apply filters if enabled
            if (g_nesFiltersEnabled) {
                mixed = applyOutputFilters(mixed);
            }

            // Same output for both channels
            outputLeft = outputRight = mixed;
        }

        // Hard clamp to [-1, 1] before 16-bit conversion to prevent overflow
        if (outputLeft > 1.0f) outputLeft = 1.0f;
        else if (outputLeft < -1.0f) outputLeft = -1.0f;
        if (outputRight > 1.0f) outputRight = 1.0f;
        else if (outputRight < -1.0f) outputRight = -1.0f;

        // Convert to 16-bit audio (-32768 to +32767)
        int16_t sampleLeft = (int16_t)(outputLeft * 32767.0f);
        int16_t sampleRight = (int16_t)(outputRight * 32767.0f);

        // Count non-zero samples for debugging
        if (sampleLeft != 0 || sampleRight != 0) {
            nonZeroSampleCount_++;
        }

        // Output stereo or mono depending on mode
        blockLeft->data[i] = sampleLeft;
        blockRight->data[i] = sampleRight;
    }

    // Transmit stereo output
    transmit(blockLeft, 0);   // Left channel
    transmit(blockRight, 1);  // Right channel

    // Release buffers
    release(blockLeft);
    release(blockRight);
}

// Frame counter ISR - called at 240Hz
void NESAPUEmulator::frameCounterISR() {
    // CRITICAL: Get local copy of instance pointer first
    NESAPUEmulator* inst = instance_;

    // Check if instance is valid and not stopping
    if (inst != nullptr) {
        // Double-check stopping flag (might have been set after instance check)
        if (!inst->stopping_) {
            inst->frameCounterTick();
        }
    }
}

// Frame counter tick - implements 4-step and 5-step sequencing
void NESAPUEmulator::frameCounterTick() {
    // CRITICAL: Check stopping flag FIRST (before accessing any members)
    if (stopping_) return;

    // Triangle linear counter ALWAYS clocks at 240Hz (every frame tick)
    triangle_.clockLinearCounter();

    if (frameMode_) {
        // 5-step mode (bit 7 = 1)
        // Step 0: (nothing)
        // Step 1: Length counter, sweep
        // Step 2: Envelope
        // Step 3: (nothing)
        // Step 4: Length counter, sweep, envelope
        switch (frameStep_) {
            case 0:
                // Nothing
                break;
            case 1:
                // Phase 4: Clock length counters (120Hz)
                pulse1_.clockLength();
                pulse2_.clockLength();
                triangle_.clockLength();
                noise_.clockLength();
                // Phase 7: Clock sweep units (120Hz)
                pulse1_.clockSweep();
                pulse2_.clockSweep();
                break;
            case 2:
                // Phase 6: Clock envelopes (240Hz)
                pulse1_.clockEnvelope();
                pulse2_.clockEnvelope();
                noise_.clockEnvelope();
                break;
            case 3:
                // Nothing
                break;
            case 4:
                // Phase 4: Clock length counters (120Hz)
                pulse1_.clockLength();
                pulse2_.clockLength();
                triangle_.clockLength();
                noise_.clockLength();
                // Phase 7: Clock sweep units (120Hz)
                pulse1_.clockSweep();
                pulse2_.clockSweep();
                // Phase 6: Clock envelopes (240Hz)
                pulse1_.clockEnvelope();
                pulse2_.clockEnvelope();
                noise_.clockEnvelope();
                break;
        }

        frameStep_++;
        if (frameStep_ >= 5) {
            frameStep_ = 0;
        }
    } else {
        // 4-step mode (bit 7 = 0)
        // Step 0: (nothing)
        // Step 1: Length counter, sweep
        // Step 2: Envelope
        // Step 3: Length counter, sweep, envelope
        switch (frameStep_) {
            case 0:
                // Nothing
                break;
            case 1:
                // Phase 4: Clock length counters (120Hz)
                pulse1_.clockLength();
                pulse2_.clockLength();
                triangle_.clockLength();
                noise_.clockLength();
                // Phase 7: Clock sweep units (120Hz)
                pulse1_.clockSweep();
                pulse2_.clockSweep();
                break;
            case 2:
                // Phase 6: Clock envelopes (240Hz)
                pulse1_.clockEnvelope();
                pulse2_.clockEnvelope();
                noise_.clockEnvelope();
                break;
            case 3:
                // Phase 4: Clock length counters (120Hz)
                pulse1_.clockLength();
                pulse2_.clockLength();
                triangle_.clockLength();
                noise_.clockLength();
                // Phase 7: Clock sweep units (120Hz)
                pulse1_.clockSweep();
                pulse2_.clockSweep();
                // Phase 6: Clock envelopes (240Hz)
                pulse1_.clockEnvelope();
                pulse2_.clockEnvelope();
                noise_.clockEnvelope();

                // Frame IRQ would fire here (but we don't need it for VGM playback)
                break;
        }

        frameStep_++;
        if (frameStep_ >= 4) {
            frameStep_ = 0;
        }
    }
}
