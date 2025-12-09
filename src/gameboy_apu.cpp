#include "gameboy_apu.h"
#include <cmath>

// Static instance for timer callback
GameBoyAPU* GameBoyAPU::instance_ = nullptr;

// Duty cycle sequences (from Pan Docs)
// 0 = 12.5%, 1 = 25%, 2 = 50%, 3 = 75%
const uint8_t GameBoyAPU::dutySequences_[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  // 12.5% (1/8)
    {1, 0, 0, 0, 0, 0, 0, 1},  // 25%   (2/8)
    {1, 0, 0, 0, 0, 1, 1, 1},  // 50%   (4/8)
    {0, 1, 1, 1, 1, 1, 1, 0},  // 75%   (6/8) - inverted 25%
};

// Noise divisor lookup table
const uint8_t GameBoyAPU::divisorTable_[8] = {
    2, 4, 8, 12, 16, 20, 24, 28  // SameBoy's divisor values (4x smaller)
};

// ========================================
// Constructor / Destructor
// ========================================

GameBoyAPU::GameBoyAPU()
    : AudioStream(0, nullptr)  // 0 inputs, stereo output created in update()
    , stopping_(false)
    , apuEnabled_(false)
    , panningLeft_(0)
    , panningRight_(0)
    , volumeLeft_(7)  // Default max volume
    , volumeRight_(7)
    , frameStep_(0)
    , clockAccumulator_(0)
    , hpf90_a_(0)
    , hpf90_x1_left_(0), hpf90_y1_left_(0)
    , hpf90_x1_right_(0), hpf90_y1_right_(0)
    , registerWriteCount_(0)
    , updateCallCount_(0)
    , nonZeroSampleCount_(0)
{
    Serial.println("[GameBoyAPU] Constructed");

    // Mark CH1 as having sweep
    pulse1_.hasSweep = true;
    pulse2_.hasSweep = false;

    // Calculate filter coefficients (for 44.1kHz sample rate)
    // Game Boy DMG high-pass filter (~28 Hz, based on hardware charge factor 0.999958)
    // This gives the GB its characteristic warm, bassy sound (vs NES's tighter 90 Hz)
    // Note: DMG has no documented hardware LPF, unlike the NES
    float omega_hpf = 2.0f * M_PI * 28.0f / SAMPLE_RATE;
    hpf90_a_ = 1.0f / (1.0f + omega_hpf);

    reset();
}

GameBoyAPU::~GameBoyAPU() {
    Serial.println("[GameBoyAPU] Destroying");
    stopFrameTimer();
    delay(10);  // ISR safety
}

// ========================================
// Reset
// ========================================

void GameBoyAPU::reset() {
    Serial.println("[GameBoyAPU] Reset to power-on state");

    memset(registers_, 0, sizeof(registers_));

    pulse1_.reset();
    pulse2_.reset();
    wave_.reset();
    noise_.reset();

    apuEnabled_ = false;
    panningLeft_ = 0;
    panningRight_ = 0;
    volumeLeft_ = 7;
    volumeRight_ = 7;
    frameStep_ = 0;
    clockAccumulator_ = 0;

    // Reset filter state
    hpf90_x1_left_ = hpf90_y1_left_ = 0;
    hpf90_x1_right_ = hpf90_y1_right_ = 0;

    registerWriteCount_ = 0;
}

// ========================================
// Frame Sequencer (512 Hz)
// ========================================

void GameBoyAPU::startFrameTimer() {
    Serial.println("[GameBoyAPU] Starting frame timer (512 Hz)");
    instance_ = this;
    frameStep_ = 0;
    stopping_ = false;
    frameTimer_.begin(frameSequencerISR, 1953);  // 1953 microseconds = 512.0 Hz
}

void GameBoyAPU::stopFrameTimer() {
    Serial.println("[GameBoyAPU] Stopping frame timer");
    stopping_ = true;
    frameTimer_.end();
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void GameBoyAPU::frameSequencerISR() {
    if (instance_ && !instance_->stopping_) {
        instance_->frameSequencerTick();
    }
}

void GameBoyAPU::frameSequencerTick() {
    // 512 Hz tick (1.953125 ms period)
    // Frame step 0-7 (8-step sequence)

    switch (frameStep_) {
        case 0:
        case 2:
        case 4:
        case 6:
            // Clock length counters (256 Hz effective)
            pulse1_.clockLength();
            pulse2_.clockLength();
            wave_.clockLength();
            noise_.clockLength();

            // Clock sweep on steps 2 and 6 (128 Hz effective)
            if (frameStep_ == 2 || frameStep_ == 6) {
                pulse1_.clockSweep();
            }
            break;

        case 7:
            // Clock envelopes (64 Hz effective)
            pulse1_.clockEnvelope();
            pulse2_.clockEnvelope();
            noise_.clockEnvelope();
            break;
    }

    frameStep_ = (frameStep_ + 1) & 0x07;  // Wrap 0-7
}

// ========================================
// Register Writes (VGM Command 0xB3)
// ========================================

void GameBoyAPU::writeRegister(uint8_t reg, uint8_t value) {
    if (reg >= 0x40) return;  // Out of range

    registers_[reg] = value;
    registerWriteCount_++;

    // Serial.printf("[GameBoyAPU] Write reg 0x%02X (GB $FF%02X) = 0x%02X\n", reg, 0x10 + reg, value);

    switch (reg) {
        // ========================================
        // CH1: Pulse with Sweep ($FF10-$FF14)
        // ========================================
        case 0x00:  // NR10 - Sweep
            pulse1_.sweepPeriod = (value >> 4) & 0x07;
            pulse1_.sweepNegate = (value & 0x08) != 0;
            pulse1_.sweepShift = value & 0x07;

            // Sweep negate direction change quirk (disable if changing from negate to add)
            // (Commented out for initial implementation - rare edge case)
            // if (pulse1_.sweepHasNegated && !pulse1_.sweepNegate) {
            //     pulse1_.enabled = false;
            // }
            break;

        case 0x01:  // NR11 - Duty + Length
            pulse1_.dutyCycle = (value >> 6) & 0x03;
            pulse1_.lengthCounter = 64 - (value & 0x3F);
            break;

        case 0x02:  // NR12 - Volume + Envelope
            pulse1_.volume = (value >> 4) & 0x0F;
            pulse1_.envelopeDirection = (value & 0x08) != 0;
            pulse1_.envelopePeriod = value & 0x07;
            pulse1_.dacEnabled = (value & 0xF8) != 0;
            if (!pulse1_.dacEnabled) {
                pulse1_.enabled = false;
            }
            break;

        case 0x03:  // NR13 - Frequency low
            // Store only, frequency updates on NR14 write
            break;

        case 0x04: { // NR14 - Trigger + Frequency high
            pulse1_.lengthEnabled = (value & 0x40) != 0;

            // Update timer period
            // Pulse timer clocks at 1.048576 MHz in hardware (master/4)
            // We clock at 2.097152 MHz (wave rate), so multiply by 2 to compensate
            uint16_t freq = ((value & 0x07) << 8) | registers_[0x03];
            pulse1_.timerPeriod = (2048 - freq) * 2;

            // Trigger
            if (value & 0x80) {
                pulse1_.trigger(freq);
            }
            break;
        }

        // ========================================
        // CH2: Pulse ($FF16-$FF19)
        // ========================================
        case 0x06:  // NR21 - Duty + Length
            pulse2_.dutyCycle = (value >> 6) & 0x03;
            pulse2_.lengthCounter = 64 - (value & 0x3F);
            break;

        case 0x07:  // NR22 - Volume + Envelope
            pulse2_.volume = (value >> 4) & 0x0F;
            pulse2_.envelopeDirection = (value & 0x08) != 0;
            pulse2_.envelopePeriod = value & 0x07;
            pulse2_.dacEnabled = (value & 0xF8) != 0;
            if (!pulse2_.dacEnabled) {
                pulse2_.enabled = false;
            }
            break;

        case 0x08:  // NR23 - Frequency low
            // Store only
            break;

        case 0x09: { // NR24 - Trigger + Frequency high
            pulse2_.lengthEnabled = (value & 0x40) != 0;

            // Pulse timer clocks at 1.048576 MHz in hardware (master/4)
            // We clock at 2.097152 MHz (wave rate), so multiply by 2 to compensate
            uint16_t freq = ((value & 0x07) << 8) | registers_[0x08];
            pulse2_.timerPeriod = (2048 - freq) * 2;

            if (value & 0x80) {
                pulse2_.trigger(freq);
            }
            break;
        }

        // ========================================
        // CH3: Wave ($FF1A-$FF1E)
        // ========================================
        case 0x0A:  // NR30 - DAC power
            wave_.dacEnabled = (value & 0x80) != 0;
            if (!wave_.dacEnabled) {
                wave_.enabled = false;
            }
            break;

        case 0x0B:  // NR31 - Length
            wave_.lengthCounter = 256 - value;
            break;

        case 0x0C:  // NR32 - Volume
            wave_.volumeShift = (value >> 5) & 0x03;
            break;

        case 0x0D:  // NR33 - Frequency low
            // Store only
            break;

        case 0x0E: { // NR34 - Trigger + Frequency high
            wave_.lengthEnabled = (value & 0x40) != 0;

            // Wave timer clocks at 2.097152 MHz in hardware (master/2) - same as our clock rate
            uint16_t freq = ((value & 0x07) << 8) | registers_[0x0D];
            wave_.timerPeriod = (2048 - freq);  // Direct 1:1 mapping

            if (value & 0x80) {
                wave_.trigger(freq);
            }
            break;
        }

        // ========================================
        // CH4: Noise ($FF20-$FF23)
        // ========================================
        case 0x10:  // NR41 - Length
            noise_.lengthCounter = 64 - (value & 0x3F);
            break;

        case 0x11:  // NR42 - Volume + Envelope
            noise_.volume = (value >> 4) & 0x0F;
            noise_.envelopeDirection = (value & 0x08) != 0;
            noise_.envelopePeriod = value & 0x07;
            noise_.dacEnabled = (value & 0xF8) != 0;
            if (!noise_.dacEnabled) {
                noise_.enabled = false;
            }
            break;

        case 0x12:  // NR43 - Noise parameters
            noise_.clockShift = (value >> 4) & 0x0F;
            noise_.widthMode = (value & 0x08) != 0;
            noise_.divisorCode = value & 0x07;
            break;

        case 0x13:  // NR44 - Trigger
            noise_.lengthEnabled = (value & 0x40) != 0;

            if (value & 0x80) {
                noise_.trigger();
            }
            break;

        // ========================================
        // Global Control ($FF24-$FF26)
        // ========================================
        case 0x14:  // NR50 - Master volume
            volumeLeft_ = (value >> 4) & 0x07;
            volumeRight_ = value & 0x07;
            break;

        case 0x15:  // NR51 - Panning
            panningLeft_ = (value >> 4) & 0x0F;
            panningRight_ = value & 0x0F;
            break;

        case 0x16:  // NR52 - Audio on/off
            if ((value & 0x80) == 0) {
                // Power off: silence all, clear registers
                Serial.println("[GameBoyAPU] APU powered OFF");
                apuEnabled_ = false;
                pulse1_.enabled = false;
                pulse2_.enabled = false;
                wave_.enabled = false;
                noise_.enabled = false;

                // Clear registers $FF10-$FF25 (VGM 0x00-0x15)
                for (int i = 0; i < 0x16; i++) {
                    registers_[i] = 0;
                }
            } else {
                Serial.println("[GameBoyAPU] APU powered ON");
                apuEnabled_ = true;
            }
            break;

        // ========================================
        // Wave RAM ($FF30-$FF3F)
        // ========================================
        case 0x20 ... 0x2F:  // Wave RAM (16 bytes)
            wave_.waveRam[reg - 0x20] = value;
            break;
    }
}

// ========================================
// Pulse Channel Methods
// ========================================

void GameBoyAPU::PulseChannel::trigger(uint16_t frequency) {
    enabled = dacEnabled;  // Can't enable if DAC is off!

    if (lengthCounter == 0) {
        lengthCounter = 64;
    }

    timerCounter = timerPeriod;
    dutyPosition = 0;

    envelopeCounter = volume;
    envelopeDivider = envelopePeriod ? envelopePeriod : 8;
    envelopeRunning = true;

    // CH1 sweep behavior
    if (hasSweep) {
        shadowFrequency = frequency;
        sweepDivider = sweepPeriod ? sweepPeriod : 8;
        sweepEnabled = (sweepPeriod > 0 || sweepShift > 0);
        sweepHasNegated = false;

        if (sweepShift > 0) {
            calculateSweepTarget();  // Immediate overflow check!
        }
    }

    Serial.printf("[GameBoyAPU] Pulse%d triggered: freq=%u, vol=%u, duty=%u\n",
                 hasSweep ? 1 : 2, frequency, volume, dutyCycle);
}

void GameBoyAPU::PulseChannel::clockTimer() {
    if (!enabled) return;

    timerCounter -= 1.0f;
    if (timerCounter <= 0) {
        timerCounter += timerPeriod;
        dutyPosition = (dutyPosition + 1) & 0x07;
    }
}

void GameBoyAPU::PulseChannel::clockLength() {
    if (lengthEnabled && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0) {
            enabled = false;
        }
    }
}

void GameBoyAPU::PulseChannel::clockEnvelope() {
    if (!envelopeRunning) return;

    if (envelopeDivider > 0) {
        envelopeDivider--;
    }

    if (envelopeDivider == 0) {
        envelopeDivider = envelopePeriod ? envelopePeriod : 8;

        if (envelopeDirection) {
            // Increase
            if (envelopeCounter < 15) {
                envelopeCounter++;
            }
        } else {
            // Decrease
            if (envelopeCounter > 0) {
                envelopeCounter--;
            }
        }

        // Stop envelope at 0 or 15
        if (envelopeCounter == 0 || envelopeCounter == 15) {
            envelopeRunning = false;
        }
    }
}

void GameBoyAPU::PulseChannel::clockSweep() {
    if (!hasSweep) return;

    if (sweepDivider > 0) {
        sweepDivider--;
    }

    if (sweepDivider == 0) {
        sweepDivider = sweepPeriod ? sweepPeriod : 8;

        if (sweepEnabled && sweepPeriod > 0) {
            uint16_t newFreq = calculateSweepTarget();

            // Overflow check
            if (newFreq > 2047) {
                enabled = false;
            } else if (sweepShift > 0) {
                shadowFrequency = newFreq;
                timerPeriod = (2048 - newFreq) * 4;

                // Recalculate to check overflow again (GB quirk!)
                calculateSweepTarget();
            }
        }
    }
}

uint16_t GameBoyAPU::PulseChannel::calculateSweepTarget() {
    uint16_t delta = shadowFrequency >> sweepShift;
    uint16_t newFreq;

    if (sweepNegate) {
        newFreq = shadowFrequency - delta;
        sweepHasNegated = true;
    } else {
        newFreq = shadowFrequency + delta;
    }

    if (newFreq > 2047) {
        enabled = false;  // Mute immediately on overflow
    }

    return newFreq;
}

void GameBoyAPU::PulseChannel::updateSweepMuting() {
    // Called during sweep clock (not yet implemented)
}

uint8_t GameBoyAPU::PulseChannel::getOutput() {
    if (!enabled || !dacEnabled) return 0;

    uint8_t bit = GameBoyAPU::dutySequences_[dutyCycle][dutyPosition];
    return bit * envelopeCounter;  // 0 or envelopeCounter
}

uint8_t GameBoyAPU::PulseChannel::getRawWaveform() {
    if (!enabled) return 0;
    return GameBoyAPU::dutySequences_[dutyCycle][dutyPosition];
}

// ========================================
// Wave Channel Methods
// ========================================

void GameBoyAPU::WaveChannel::trigger(uint16_t frequency) {
    enabled = dacEnabled;

    if (lengthCounter == 0) {
        lengthCounter = 256;
    }

    timerCounter = timerPeriod;
    samplePosition = 1;  // Hardware starts at position 1 (Pan Docs confirmed)

    Serial.printf("[GameBoyAPU] Wave triggered: freq=%u, volumeShift=%u\n", frequency, volumeShift);
}

void GameBoyAPU::WaveChannel::clockTimer() {
    if (!enabled) return;

    timerCounter -= 1.0f;
    if (timerCounter <= 0) {
        timerCounter += timerPeriod;
        samplePosition = (samplePosition + 1) & 0x1F;  // Wrap 0-31
    }
}

void GameBoyAPU::WaveChannel::clockLength() {
    if (lengthEnabled && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0) {
            enabled = false;
        }
    }
}

uint8_t GameBoyAPU::WaveChannel::getCurrentSample() {
    uint8_t byte = waveRam[samplePosition >> 1];  // Each byte = 2 samples

    if (samplePosition & 1) {
        return byte & 0x0F;  // Odd position = lower nibble
    } else {
        return byte >> 4;    // Even position = upper nibble
    }
}

uint8_t GameBoyAPU::WaveChannel::getOutput() {
    if (!enabled || !dacEnabled) return 0;

    uint8_t sample = getCurrentSample();  // 0-15

    // Apply volume shift
    switch (volumeShift) {
        case 0: return 0;              // Mute
        case 1: return sample;         // 100%
        case 2: return sample >> 1;    // 50%
        case 3: return sample >> 2;    // 25%
    }
    return 0;
}

// ========================================
// Noise Channel Methods
// ========================================

void GameBoyAPU::NoiseChannel::trigger() {
    enabled = dacEnabled;

    if (lengthCounter == 0) {
        lengthCounter = 64;
    }

    lfsr = 0x7FFF;  // All 15 bits set to 1 (confirmed by Gambatte source)
    timerCounter = getTimerPeriod();

    envelopeCounter = volume;
    envelopeDivider = envelopePeriod ? envelopePeriod : 8;
    envelopeRunning = true;

    Serial.printf("[GameBoyAPU] Noise triggered: shift=%u, div=%u, width=%s\n",
                 clockShift, divisorCode, widthMode ? "7-bit" : "15-bit");
}

void GameBoyAPU::NoiseChannel::shiftLFSR() {
    // XOR bits 0 and 1
    uint8_t xor_result = (lfsr & 0x01) ^ ((lfsr >> 1) & 0x01);

    // Shift right
    lfsr >>= 1;

    // Store XOR result in bit 14
    lfsr |= (xor_result << 14);

    // If 7-bit mode, ALSO store in bit 6
    if (widthMode) {
        lfsr &= ~0x40;  // Clear bit 6
        lfsr |= (xor_result << 6);
    }
}

void GameBoyAPU::NoiseChannel::clockTimer() {
    if (!enabled) return;

    // Clock shift 14/15 prevents clocking (GB quirk)
    if (clockShift >= 14) return;

    timerCounter -= 1.0f;
    if (timerCounter <= 0) {
        timerCounter += getTimerPeriod();
        shiftLFSR();
    }
}

void GameBoyAPU::NoiseChannel::clockEnvelope() {
    if (!envelopeRunning) return;

    if (envelopeDivider > 0) {
        envelopeDivider--;
    }

    if (envelopeDivider == 0) {
        envelopeDivider = envelopePeriod ? envelopePeriod : 8;

        if (envelopeDirection) {
            if (envelopeCounter < 15) {
                envelopeCounter++;
            }
        } else {
            if (envelopeCounter > 0) {
                envelopeCounter--;
            }
        }

        if (envelopeCounter == 0 || envelopeCounter == 15) {
            envelopeRunning = false;
        }
    }
}

void GameBoyAPU::NoiseChannel::clockLength() {
    if (lengthEnabled && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0) {
            enabled = false;
        }
    }
}

uint16_t GameBoyAPU::NoiseChannel::getTimerPeriod() {
    uint8_t divisor = GameBoyAPU::divisorTable_[divisorCode];
    // Timer period = divisor * 2^shift
    // We clock at 1.048576 MHz (every other iteration at 2.097152 MHz rate)
    // Formula: period = divisor << shift
    return divisor << clockShift;
}

uint8_t GameBoyAPU::NoiseChannel::getOutput() {
    if (!enabled || !dacEnabled) return 0;

    // Bit 0 of LFSR = output, but INVERTED!
    uint8_t bit = (~lfsr) & 0x01;

    return bit * envelopeCounter;  // 0 or envelopeCounter
}

// ========================================
// Mixing (Stereo with Hardware Panning)
// ========================================

void GameBoyAPU::mixChannelsStereo(
    uint8_t pulse1Out, uint8_t pulse2Out,
    uint8_t waveOut, uint8_t noiseOut,
    float& outLeft, float& outRight
) {
    if (!apuEnabled_) {
        outLeft = outRight = 0.0f;
        return;
    }

    // Mix left channel (bits 7-4 of NR51)
    float left = 0.0f;
    if (panningLeft_ & 0x01) left += pulse1Out;  // CH1
    if (panningLeft_ & 0x02) left += pulse2Out;  // CH2
    if (panningLeft_ & 0x04) left += waveOut;    // CH3
    if (panningLeft_ & 0x08) left += noiseOut;   // CH4

    // Mix right channel (bits 3-0 of NR51)
    float right = 0.0f;
    if (panningRight_ & 0x01) right += pulse1Out;
    if (panningRight_ & 0x02) right += pulse2Out;
    if (panningRight_ & 0x04) right += waveOut;
    if (panningRight_ & 0x08) right += noiseOut;

    // Apply master volume (0-7) -> normalize to 0.0-1.0
    left *= (volumeLeft_ + 1) / 8.0f;
    right *= (volumeRight_ + 1) / 8.0f;

    // Normalize: 4 channels * 15 (max per channel) = 60 max
    left /= 60.0f;
    right /= 60.0f;

    outLeft = left;
    outRight = right;
}

// ========================================
// Output Filters
// ========================================

float GameBoyAPU::applyOutputFiltersLeft(float x) {
    // Game Boy DMG high-pass filter (~28 Hz, removes DC offset)
    // Note: DMG has no documented hardware LPF (unlike NES)
    float hpf_out = hpf90_a_ * (hpf90_y1_left_ + x - hpf90_x1_left_);
    hpf90_x1_left_ = x;
    hpf90_y1_left_ = hpf_out;

    return hpf_out;
}

float GameBoyAPU::applyOutputFiltersRight(float x) {
    // Game Boy DMG high-pass filter (~28 Hz)
    // Note: DMG has no documented hardware LPF (unlike NES)
    float hpf_out = hpf90_a_ * (hpf90_y1_right_ + x - hpf90_x1_right_);
    hpf90_x1_right_ = x;
    hpf90_y1_right_ = hpf_out;

    return hpf_out;
}

// ========================================
// AudioStream Update (44.1kHz)
// ========================================

void GameBoyAPU::update() {
    if (stopping_) return;

    audio_block_t* blockLeft = allocate();
    audio_block_t* blockRight = allocate();
    if (!blockLeft || !blockRight) {
        if (blockLeft) release(blockLeft);
        if (blockRight) release(blockRight);
        return;
    }

    updateCallCount_++;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        // Clock timers for sub-sample accuracy
        float clocksThisSample = TIMER_CLOCKS_PER_SAMPLE + clockAccumulator_;
        int clocksToRun = (int)clocksThisSample;
        clockAccumulator_ = clocksThisSample - clocksToRun;

        for (int c = 0; c < clocksToRun; c++) {
            pulse1_.clockTimer();
            pulse2_.clockTimer();
            wave_.clockTimer();

            // Noise clocks at half rate (1.048576 MHz vs 2.097152 MHz)
            if (c & 1) {
                noise_.clockTimer();
            }
        }

        // Get channel outputs (0-15)
        uint8_t p1 = pulse1_.getOutput();
        uint8_t p2 = pulse2_.getOutput();
        uint8_t wav = wave_.getOutput();
        uint8_t noi = noise_.getOutput();

        // Mix with panning
        float left, right;
        mixChannelsStereo(p1, p2, wav, noi, left, right);

        // Apply output filter (HPF only - DMG has no hardware LPF)
        left = applyOutputFiltersLeft(left);
        right = applyOutputFiltersRight(right);

        // Convert to int16
        blockLeft->data[i] = (int16_t)(left * 32767.0f);
        blockRight->data[i] = (int16_t)(right * 32767.0f);

        // Track non-zero samples for debug
        if (blockLeft->data[i] != 0 || blockRight->data[i] != 0) {
            nonZeroSampleCount_++;
        }
    }

    transmit(blockLeft, 0);
    transmit(blockRight, 1);
    release(blockLeft);
    release(blockRight);
}
