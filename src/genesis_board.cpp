/**
 * @file genesis_board.cpp
 * @brief Implementation of Genesis synthesizer board hardware control
 *
 * TIMING MODEL:
 * - Smart timing: tracks lastWriteTime_ and only delays if needed
 * - YM2612: 5μs between data writes (BUSY flag, with margin)
 * - SN76489: 9μs between writes
 * - Shift register: no explicit settling delay (74HCT164 is ~40ns, digitalWrite overhead is ~100ns)
 * - WR pulse: 200ns minimum, we use ~100ns (digitalWrite overhead)
 */

#include "genesis_board.h"

// ========== Initialization ==========

void GenesisBoard::begin(const Config& config) {
    config_ = config;
    dacEnabled_ = false;
    dacStreamMode_ = false;
    lastError_ = nullptr;
    debugMode_ = false;
    lastWriteTime_ = 0;
    psgAttenuateForMix_ = false;

    // Configure all pins as outputs
    pinMode(config_.pinWrSN, OUTPUT);
    pinMode(config_.pinWrYM, OUTPUT);
    pinMode(config_.pinIcYM, OUTPUT);
    pinMode(config_.pinA0YM, OUTPUT);
    pinMode(config_.pinA1YM, OUTPUT);
    pinMode(config_.pinSCK, OUTPUT);
    pinMode(config_.pinSDI, OUTPUT);

    // Set initial states (all control signals inactive)
    digitalWrite(config_.pinWrSN, HIGH);  // PSG write strobe inactive
    digitalWrite(config_.pinWrYM, HIGH);  // YM2612 write strobe inactive
    digitalWrite(config_.pinIcYM, HIGH);  // YM2612 not in reset
    digitalWrite(config_.pinA0YM, LOW);   // Address mode
    digitalWrite(config_.pinA1YM, LOW);   // Port 0
    digitalWrite(config_.pinSCK, LOW);    // SPI clock idle
    digitalWrite(config_.pinSDI, LOW);    // SPI data idle

    initialized_ = true;

    // Perform hardware reset
    hardwareReset();
    reset();

    Serial.println("Genesis board initialized");
    Serial.print("  Pins: WR_SN="); Serial.print(config_.pinWrSN);
    Serial.print(", WR_YM="); Serial.print(config_.pinWrYM);
    Serial.print(", IC_YM="); Serial.print(config_.pinIcYM);
    Serial.print(", A0="); Serial.print(config_.pinA0YM);
    Serial.print(", A1="); Serial.print(config_.pinA1YM);
    Serial.print(", SCK="); Serial.print(config_.pinSCK);
    Serial.print(", SDI="); Serial.println(config_.pinSDI);
    Serial.println("  Clocks: On-board (SN76489 @ 3.58MHz, YM2612 @ 7.68MHz)");
    Serial.println("  Timing: Smart (YM=5us, PSG=9us between writes)");
}

void GenesisBoard::reset() {
    if (!initialized_) return;

    hardwareReset();
    enableDAC(false);

    // Key off all YM2612 channels
    for (uint8_t ch = 0; ch < 6; ch++) {
        writeYM2612(0, 0x28, ch);
    }

    silencePSG();

    if (debugMode_) {
        Serial.println("Genesis board reset complete");
    }
}

void GenesisBoard::hardwareReset() {
    digitalWrite(config_.pinIcYM, LOW);
    delay(10);
    digitalWrite(config_.pinIcYM, HIGH);
    delay(10);

    // Reset timing state
    lastWriteTime_ = 0;
    dacStreamMode_ = false;
}

// ========== SN76489 PSG Control ==========

void GenesisBoard::writePSG(uint8_t value) {
    if (!initialized_) {
        lastError_ = "Genesis board not initialized";
        return;
    }

    // Exit DAC streaming mode if active (changes shift register contents)
    if (dacStreamMode_) {
        endDACStream();
    }

    // Apply volume attenuation if enabled
    if (psgAttenuateForMix_ && (value & 0x90) == 0x90) {
        uint8_t attenuation = value & 0x0F;
        value = (value & 0xF0) | PSG_ATTENUATION_MAP[attenuation];
    }

    // Wait for PSG busy time from last write (PSG or YM)
    waitIfNeeded(PSG_BUSY_US);

    // CRITICAL: Disable interrupts for entire write sequence
    noInterrupts();

    // PSG write sequence:
    // 1. Ensure WR is HIGH
    // 2. Shift data into register (bit-reversed for new board wiring)
    // 3. Pulse WR LOW to latch

    digitalWrite(config_.pinWrSN, HIGH);
    spiTransfer(reverseBits(value));  // Bit-reversed for new board wiring

    // WR pulse - PSG needs longer pulse than YM2612
    digitalWrite(config_.pinWrSN, LOW);
    delayMicroseconds(8);  // 8μs WR pulse width
    digitalWrite(config_.pinWrSN, HIGH);

    interrupts();

    lastWriteTime_ = micros();

    if (debugMode_) {
        Serial.print("PSG: 0x");
        Serial.println(value, HEX);
    }
}

void GenesisBoard::silencePSG() {
    writePSG(0x9F);  // Channel 0 off
    writePSG(0xBF);  // Channel 1 off
    writePSG(0xDF);  // Channel 2 off
    writePSG(0xFF);  // Noise off
}

// ========== YM2612 FM Control ==========

void GenesisBoard::writeYM2612(uint8_t port, uint8_t reg, uint8_t value) {
    if (!initialized_) {
        lastError_ = "Genesis board not initialized";
        return;
    }

    if (!validatePort(port)) {
        lastError_ = "Invalid YM2612 port";
        return;
    }

    // Exit DAC streaming mode if active
    if (dacStreamMode_) {
        endDACStream();
    }

    // Wait for YM2612 busy time from last data write
    // Address writes don't trigger BUSY, but we track all writes uniformly
    waitIfNeeded(YM_BUSY_US);

    // CRITICAL: Disable interrupts for entire write sequence
    // An interrupt between spiTransfer and WR pulse could corrupt the write
    noInterrupts();

    // === ADDRESS PHASE ===
    digitalWrite(config_.pinA1YM, port ? HIGH : LOW);
    digitalWrite(config_.pinA0YM, LOW);  // Address mode

    spiTransfer(reg);
    delayMicroseconds(4);  // Settling after transfer

    // WR pulse for address
    digitalWrite(config_.pinWrYM, LOW);
    delayMicroseconds(1);
    digitalWrite(config_.pinWrYM, HIGH);
    delayMicroseconds(1);  // Bus hold

    // === DATA PHASE ===
    digitalWrite(config_.pinA0YM, HIGH);  // Data mode

    spiTransfer(value);
    delayMicroseconds(4);  // Settling after transfer

    // WR pulse for data - THIS triggers BUSY
    digitalWrite(config_.pinWrYM, LOW);
    delayMicroseconds(1);
    digitalWrite(config_.pinWrYM, HIGH);

    // Return to idle state
    digitalWrite(config_.pinA0YM, LOW);

    interrupts();

    // Record time of data write (the one that triggers BUSY)
    lastWriteTime_ = micros();

    if (debugMode_) {
        Serial.print("YM P");
        Serial.print(port);
        Serial.print(" R0x");
        Serial.print(reg, HEX);
        Serial.print("=0x");
        Serial.println(value, HEX);
    }
}

// ========== DAC Control ==========

void GenesisBoard::enableDAC(bool enable) {
    if (dacEnabled_ == enable) return;

    dacEnabled_ = enable;
    writeYM2612(0, YM2612_DAC_ENABLE, enable ? 0x80 : 0x00);

    if (debugMode_) {
        Serial.print("DAC ");
        Serial.println(enable ? "enabled" : "disabled");
    }
}

void GenesisBoard::writeDAC(uint8_t sample) {
    if (!initialized_) return;

    // Enter streaming mode if not already (latches address 0x2A)
    if (!dacStreamMode_) {
        beginDACStream();
    }

    // In streaming mode, we only need to write data (A0 is already HIGH)
    // Wait for YM busy time
    waitIfNeeded(YM_BUSY_US);

    noInterrupts();

    spiTransfer(sample);
    delayMicroseconds(4);  // Settling after transfer

    digitalWrite(config_.pinWrYM, LOW);
    delayMicroseconds(1);
    digitalWrite(config_.pinWrYM, HIGH);

    interrupts();

    lastWriteTime_ = micros();
}

void GenesisBoard::beginDACStream() {
    if (dacStreamMode_) return;

    // Wait for any pending busy
    waitIfNeeded(YM_BUSY_US);

    noInterrupts();

    // Write address 0x2A to latch it
    digitalWrite(config_.pinA1YM, LOW);   // Port 0
    digitalWrite(config_.pinA0YM, LOW);   // Address mode

    spiTransfer(YM2612_DAC_DATA);
    delayMicroseconds(4);  // Settling after transfer

    digitalWrite(config_.pinWrYM, LOW);
    delayMicroseconds(1);
    digitalWrite(config_.pinWrYM, HIGH);
    delayMicroseconds(1);  // Bus hold

    // Leave A0 HIGH for subsequent data writes
    digitalWrite(config_.pinA0YM, HIGH);

    interrupts();

    dacStreamMode_ = true;

    if (debugMode_) {
        Serial.println("DAC stream started");
    }
}

void GenesisBoard::endDACStream() {
    if (!dacStreamMode_) return;

    digitalWrite(config_.pinA0YM, LOW);
    dacStreamMode_ = false;

    if (debugMode_) {
        Serial.println("DAC stream ended");
    }
}

// ========== Private Helper Functions ==========

uint8_t GenesisBoard::reverseBits(uint8_t data) {
    // Reverse all 8 bits using parallel swap technique
    // Needed for PSG: SPI sends MSB first, but new board wiring is QA→D0
    data = (data & 0xF0) >> 4 | (data & 0x0F) << 4;
    data = (data & 0xCC) >> 2 | (data & 0x33) << 2;
    data = (data & 0xAA) >> 1 | (data & 0x55) << 1;
    return data;
}

void GenesisBoard::spiTransfer(uint8_t data) {
    // Bit-bang SPI transfer (MSB first)
    // Note: Using digitalWrite (not digitalWriteFast) because pins are runtime variables
    // Note: Caller is responsible for disabling interrupts if needed
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(config_.pinSDI, (data >> i) & 0x01);
        digitalWrite(config_.pinSCK, HIGH);
        digitalWrite(config_.pinSCK, LOW);
    }
}

bool GenesisBoard::validatePort(uint8_t port) {
    if (port > 1) {
        if (debugMode_) {
            Serial.print("ERROR: Invalid YM2612 port ");
            Serial.println(port);
        }
        return false;
    }
    return true;
}
