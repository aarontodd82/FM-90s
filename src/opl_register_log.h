#ifndef OPL_REGISTER_LOG_H
#define OPL_REGISTER_LOG_H

#include <Arduino.h>
#include <cstdint>

/**
 * OPL Register Logger - Captures register writes for visualization
 *
 * This hooks into the OPL3Duo library to capture all register writes
 * and stores them in a circular buffer for display on the Now Playing screen.
 *
 * Shows a real-time stream of what's being sent to the OPL chips,
 * making it format-agnostic (works for MIDI, VGM, DRO, FM90s, etc.)
 */

struct OPLRegisterWrite {
    uint8_t chip;       // 0 or 1 (OPL3 Duo has 2 chips)
    uint16_t reg;       // Register address (0x000-0x1FF)
    uint8_t value;      // Value written
    uint32_t timestamp; // millis() when written

    // Decoded info for display
    const char* getRegisterName() const {
        // High-level register categories
        if (reg >= 0x20 && reg <= 0x35) return "MULT/KSR";      // Tremolo/Vibrato/Sustain
        if (reg >= 0x40 && reg <= 0x55) return "LEVEL/KSL";     // Key Scale Level / Output Level
        if (reg >= 0x60 && reg <= 0x75) return "ATTACK/DECAY";  // Attack Rate / Decay Rate
        if (reg >= 0x80 && reg <= 0x95) return "SUSTAIN/REL";   // Sustain Level / Release Rate
        if (reg >= 0xA0 && reg <= 0xA8) return "FREQ-LO";       // Frequency Low 8 bits
        if (reg >= 0xB0 && reg <= 0xB8) return "FREQ-HI/ON";    // Frequency High + Key On
        if (reg >= 0xC0 && reg <= 0xC8) return "FEEDBACK/ALG";  // Feedback / Algorithm
        if (reg >= 0xE0 && reg <= 0xF5) return "WAVEFORM";      // Waveform Select

        if (reg == 0x01) return "WSE"; // Waveform Select Enable
        if (reg == 0x04) return "4OP"; // 4-operator enable
        if (reg == 0x05) return "OPL3"; // OPL3 mode enable
        if (reg == 0x08) return "NOTE-SEL"; // Composite sine/note select
        if (reg == 0xBD) return "RHYTHM"; // Rhythm control

        return "OTHER";
    }

    // Get channel number if applicable
    int getChannel() const {
        if (reg >= 0xA0 && reg <= 0xA8) return (reg - 0xA0);
        if (reg >= 0xB0 && reg <= 0xB8) return (reg - 0xB0);
        if (reg >= 0xC0 && reg <= 0xC8) return (reg - 0xC0);
        return -1; // Not a channel-specific register
    }

    // Get operator number (for operator-specific registers)
    int getOperator() const {
        if (reg >= 0x20 && reg <= 0x35) return (reg - 0x20) % 32;
        if (reg >= 0x40 && reg <= 0x55) return (reg - 0x40) % 32;
        if (reg >= 0x60 && reg <= 0x75) return (reg - 0x60) % 32;
        if (reg >= 0x80 && reg <= 0x95) return (reg - 0x80) % 32;
        if (reg >= 0xE0 && reg <= 0xF5) return (reg - 0xE0) % 32;
        return -1; // Not operator-specific
    }

    // Decode register value to human-readable string (max 50 chars)
    void getDecoded(char* buf, size_t bufSize) const {
        if (reg >= 0xB0 && reg <= 0xB8) {
            // FREQ-HI/ON: bits [5] = key on, [4:2] = octave, [1:0] = freq high
            bool keyOn = (value & 0x20) != 0;
            uint8_t octave = (value >> 2) & 0x07;
            uint8_t freqHi = value & 0x03;
            snprintf(buf, bufSize, "%s Oct=%d FHi=%d", keyOn ? "KeyOn" : "KeyOff", octave, freqHi);
        }
        else if (reg >= 0xA0 && reg <= 0xA8) {
            // FREQ-LO: full 8-bit frequency low
            snprintf(buf, bufSize, "FLo=%d", value);
        }
        else if (reg >= 0x40 && reg <= 0x55) {
            // LEVEL/KSL: bits [7:6] = KSL, [5:0] = total level (0=loud, 63=silent)
            uint8_t ksl = (value >> 6) & 0x03;
            uint8_t level = value & 0x3F;
            uint8_t vol = 63 - level;  // Invert for human readability
            snprintf(buf, bufSize, "Vol=%d/63 KSL=%d", vol, ksl);
        }
        else if (reg >= 0x20 && reg <= 0x35) {
            // MULT/KSR: bits [5] = AM, [4] = VIB, [3] = EG, [2] = KSR, [1:0] = MULT
            bool am = (value & 0x80) != 0;
            bool vib = (value & 0x40) != 0;
            bool egt = (value & 0x20) != 0;
            bool ksr = (value & 0x10) != 0;
            uint8_t mult = value & 0x0F;
            snprintf(buf, bufSize, "M=%d %s%s%s%s", mult,
                    am ? "AM " : "", vib ? "Vib " : "", egt ? "Sus " : "", ksr ? "KSR" : "");
        }
        else if (reg >= 0x60 && reg <= 0x75) {
            // ATTACK/DECAY: bits [7:4] = attack, [3:0] = decay
            uint8_t attack = (value >> 4) & 0x0F;
            uint8_t decay = value & 0x0F;
            snprintf(buf, bufSize, "Att=%d Dec=%d", attack, decay);
        }
        else if (reg >= 0x80 && reg <= 0x95) {
            // SUSTAIN/RELEASE: bits [7:4] = sustain, [3:0] = release
            uint8_t sustain = (value >> 4) & 0x0F;
            uint8_t release = value & 0x0F;
            snprintf(buf, bufSize, "Sus=%d Rel=%d", sustain, release);
        }
        else if (reg >= 0xC0 && reg <= 0xC8) {
            // FEEDBACK/ALG: bits [3:1] = feedback, [0] = algorithm
            uint8_t feedback = (value >> 1) & 0x07;
            bool additive = (value & 0x01) != 0;
            snprintf(buf, bufSize, "FB=%d %s", feedback, additive ? "Additive" : "FM");
        }
        else if (reg >= 0xE0 && reg <= 0xF5) {
            // WAVEFORM: bits [2:0] = waveform select
            uint8_t wave = value & 0x07;
            const char* waveNames[] = {"Sine", "HalfSine", "AbsSine", "PulseSine", "SinEven", "AbsEven", "Square", "DerivedSq"};
            snprintf(buf, bufSize, "Wave=%s", wave < 8 ? waveNames[wave] : "???");
        }
        else if (reg == 0xBD) {
            // RHYTHM: percussion mode
            bool dam = (value & 0x80) != 0;
            bool dvb = (value & 0x40) != 0;
            bool rhy = (value & 0x20) != 0;
            bool bd = (value & 0x10) != 0;
            bool sd = (value & 0x08) != 0;
            bool tt = (value & 0x04) != 0;
            bool tc = (value & 0x02) != 0;
            bool hh = (value & 0x01) != 0;
            snprintf(buf, bufSize, "%s%s%s%s%s%s%s%s",
                    rhy ? "Drums " : "Melodic ",
                    bd ? "BD " : "", sd ? "SD " : "", tt ? "TT " : "",
                    tc ? "TC " : "", hh ? "HH " : "",
                    dam ? "DAM " : "", dvb ? "DVB" : "");
        }
        else {
            // Generic hex display for other registers
            snprintf(buf, bufSize, "0x%02X", value);
        }
    }
};

class OPLRegisterLog {
private:
    static const int BUFFER_SIZE = 256; // Circular buffer size
    OPLRegisterWrite buffer[BUFFER_SIZE];
    int writeIndex;
    int readIndex;
    int count;
    bool enabled;

    uint32_t totalWrites;  // Statistics
    uint32_t lastSecondWrites;
    uint32_t lastSecondTime;
    uint32_t firstTimestamp;  // Timestamp of first write (for relative time)

public:
    OPLRegisterLog() : writeIndex(0), readIndex(0), count(0), enabled(true),
                       totalWrites(0), lastSecondWrites(0), lastSecondTime(0), firstTimestamp(0) {}

    // Enable/disable logging
    void setEnabled(bool enable) { enabled = enable; }
    bool isEnabled() const { return enabled; }

    // Log a register write
    void logWrite(uint8_t chip, uint16_t reg, uint8_t value) {
        if (!enabled) return;

        uint32_t now = millis();

        // Record first timestamp for relative time calculations
        if (totalWrites == 0) {
            firstTimestamp = now;
        }

        buffer[writeIndex].chip = chip;
        buffer[writeIndex].reg = reg;
        buffer[writeIndex].value = value;
        buffer[writeIndex].timestamp = now;

        writeIndex = (writeIndex + 1) % BUFFER_SIZE;

        if (count < BUFFER_SIZE) {
            count++;
        } else {
            // Buffer full - overwrite oldest
            readIndex = (readIndex + 1) % BUFFER_SIZE;
        }

        totalWrites++;
        lastSecondWrites++;

        // Calculate writes per second (reuse 'now' from above)
        if (now - lastSecondTime >= 1000) {
            lastSecondTime = now;
            lastSecondWrites = 0;
        }
    }

    // Get number of entries in buffer
    int getCount() const { return count; }

    // Get the most recent N entries (for display)
    // Returns actual number retrieved (may be less than requested)
    // Thread-safe: Disables interrupts during buffer read to prevent race conditions
    int getRecent(OPLRegisterWrite* dest, int maxCount) const {
        if (count == 0 || maxCount == 0) return 0;

        // Disable interrupts to prevent logWrite() from modifying buffer mid-read
        __disable_irq();

        int available = min(count, maxCount);
        int idx = (writeIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE;

        for (int i = 0; i < available; i++) {
            dest[i] = buffer[idx];
            idx = (idx - 1 + BUFFER_SIZE) % BUFFER_SIZE;
        }

        __enable_irq();

        return available;
    }

    // Clear the buffer
    void clear() {
        writeIndex = 0;
        readIndex = 0;
        count = 0;
        totalWrites = 0;
        lastSecondWrites = 0;
    }

    // Get statistics
    uint32_t getTotalWrites() const { return totalWrites; }
    uint32_t getFirstTimestamp() const { return firstTimestamp; }
    uint32_t getWritesPerSecond() const {
        uint32_t elapsed = millis() - lastSecondTime;
        if (elapsed >= 1000) return 0; // Stale data
        return lastSecondWrites;
    }
};

// Global instance
extern OPLRegisterLog g_oplLog;

#endif // OPL_REGISTER_LOG_H
