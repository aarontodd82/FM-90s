#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <Arduino.h>
#include <Adafruit_RGBLCDShield.h>

/**
 * LCDManager - Time-sliced LCD update manager with ZERO blocking
 *
 * Purpose: Eliminate I2C blocking during audio playback by:
 * - Time-slicing: Sends ONE character per update() call
 * - Spacing: Configurable delay between characters (default 1ms)
 * - Dirty checking: Only updates if content changed
 * - Smart clearing: Overwrites with spaces instead of lcd->clear()
 *
 * How Time-Slicing Works:
 * Instead of sending all I2C data in one blocking call:
 *   OLD: setCursor + print(16 chars) = 8ms blocking @ 100kHz
 *   NEW: Each update() sends 1 char (~0.5ms @ 400kHz), waits 3ms, repeat
 *        Spreads across ~100ms total, ~0.5ms blocking per iteration
 *
 * Audio ISR runs every 2.9ms, so 3ms spacing guarantees it can always run!
 *
 * Usage:
 *   LCDManager lcdMgr(lcd);
 *
 *   // Update content (doesn't send immediately)
 *   lcdMgr.setLine(0, "Now Playing");
 *   lcdMgr.setLineF(1, "Track %d/%d", current, total);
 *
 *   // Call from main loop - sends ONE char per call (with spacing)
 *   lcdMgr.update();
 */
class LCDManager {
private:
    Adafruit_RGBLCDShield* lcd_;

    // Content tracking (dirty checking)
    char line0_[17];          // Current content on LCD line 0
    char line1_[17];          // Current content on LCD line 1
    char pendingLine0_[17];   // Pending content for line 0
    char pendingLine1_[17];   // Pending content for line 1

    // Time-slicing state machine
    enum UpdateState {
        IDLE,                   // No update in progress
        SET_CURSOR_LINE0,       // Send setCursor(0, 0)
        PRINT_LINE0_CHAR,       // Send one character of line 0
        SET_CURSOR_LINE1,       // Send setCursor(0, 1)
        PRINT_LINE1_CHAR,       // Send one character of line 1
        COMPLETE                // Update finished
    };

    UpdateState state_;
    int charIndex_;             // Current character being sent (0-15)
    bool line0NeedsUpdate_;     // Does line 0 need updating?
    bool line1NeedsUpdate_;     // Does line 1 need updating?

    // Spacing between characters to give audio ISR time
    uint32_t lastCharSendTime_;
    static const uint32_t MIN_CHAR_INTERVAL_MS = 3;  // 3ms between chars (audio ISR is every 2.9ms)

public:
    /**
     * Constructor
     * @param lcd - Pointer to Adafruit_RGBLCDShield instance
     */
    LCDManager(Adafruit_RGBLCDShield* lcd)
        : lcd_(lcd)
        , state_(IDLE)
        , charIndex_(0)
        , line0NeedsUpdate_(false)
        , line1NeedsUpdate_(false)
        , lastCharSendTime_(0)
    {
        // Initialize with spaces
        memset(line0_, ' ', 16);
        line0_[16] = '\0';
        memset(line1_, ' ', 16);
        line1_[16] = '\0';
        memset(pendingLine0_, ' ', 16);
        pendingLine0_[16] = '\0';
        memset(pendingLine1_, ' ', 16);
        pendingLine1_[16] = '\0';
    }

    /**
     * Set line content (doesn't update LCD immediately, just marks dirty)
     * @param line - Line number (0 or 1)
     * @param text - Text to display (will be padded/truncated to 16 chars)
     */
    void setLine(uint8_t line, const char* text) {
        if (line > 1 || !text) return;

        char* target = (line == 0) ? pendingLine0_ : pendingLine1_;

        // Copy up to 16 characters
        size_t len = strlen(text);
        if (len > 16) len = 16;

        memcpy(target, text, len);

        // Pad with spaces (no need for lcd->clear()!)
        if (len < 16) {
            memset(target + len, ' ', 16 - len);
        }
        target[16] = '\0';

        // Mark as needing update (will be picked up by next update() call)
        if (line == 0) {
            line0NeedsUpdate_ = true;
        } else {
            line1NeedsUpdate_ = true;
        }
    }

    /**
     * Build a line with printf-style formatting
     * @param line - Line number (0 or 1)
     * @param format - Printf format string
     * @param ... - Format arguments
     */
    void setLineF(uint8_t line, const char* format, ...) {
        char buffer[17];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        setLine(line, buffer);
    }

    /**
     * Clear a line (fill with spaces)
     * @param line - Line number (0 or 1)
     */
    void clearLine(uint8_t line) {
        setLine(line, "");
    }

    /**
     * Clear both lines
     */
    void clear() {
        clearLine(0);
        clearLine(1);
    }

    /**
     * Update the LCD - TIME-SLICED with SPACING!
     * Call this from your main loop. It sends ONE character per call,
     * with minimum spacing between sends.
     *
     * State machine:
     * 1. IDLE: Check if update needed, start if content changed
     * 2. SET_CURSOR_LINE0: Send setCursor(0, 0) if line 0 changed
     * 3. PRINT_LINE0_CHAR: Send 1 char of line 0 (repeats 16 times, 3ms apart)
     * 4. SET_CURSOR_LINE1: Send setCursor(0, 1) if line 1 changed
     * 5. PRINT_LINE1_CHAR: Send 1 char of line 1 (repeats 16 times, 3ms apart)
     * 6. COMPLETE: Finish update, return to IDLE
     *
     * @return true if actively updating (not idle)
     */
    bool update() {
        switch (state_) {
            case IDLE: {
                // Check if content actually changed (dirty checking)
                bool line0Changed = (memcmp(line0_, pendingLine0_, 16) != 0);
                bool line1Changed = (memcmp(line1_, pendingLine1_, 16) != 0);

                if (!line0Changed && !line1Changed) {
                    line0NeedsUpdate_ = false;
                    line1NeedsUpdate_ = false;
                    return false;  // Nothing to update
                }

                // Start update
                if (line0Changed) {
                    line0NeedsUpdate_ = true;
                    state_ = SET_CURSOR_LINE0;
                    charIndex_ = 0;
                } else if (line1Changed) {
                    line1NeedsUpdate_ = true;
                    state_ = SET_CURSOR_LINE1;
                    charIndex_ = 0;
                } else {
                    line0NeedsUpdate_ = false;
                    line1NeedsUpdate_ = false;
                }
                return true;
            }

            case SET_CURSOR_LINE0: {
                // Check spacing (setCursor doesn't need spacing, but keep consistent)
                uint32_t now = millis();
                if (now - lastCharSendTime_ < MIN_CHAR_INTERVAL_MS) {
                    return true;  // Not ready yet, try again next loop
                }

                // Send setCursor command (~0.5ms blocking @ 400kHz)
                lcd_->setCursor(0, 0);
                lastCharSendTime_ = now;
                state_ = PRINT_LINE0_CHAR;
                charIndex_ = 0;
                return true;
            }

            case PRINT_LINE0_CHAR: {
                // Check spacing - ensure 3ms between characters
                uint32_t now = millis();
                if (now - lastCharSendTime_ < MIN_CHAR_INTERVAL_MS) {
                    return true;  // Not ready yet, try again next loop
                }

                // Send ONE character (~0.5ms blocking @ 400kHz)
                lcd_->write(pendingLine0_[charIndex_]);
                lastCharSendTime_ = now;

                charIndex_++;
                if (charIndex_ >= 16) {
                    // Line 0 complete - copy to current buffer
                    memcpy(line0_, pendingLine0_, 16);
                    line0NeedsUpdate_ = false;

                    // Move to line 1 if needed
                    if (line1NeedsUpdate_) {
                        state_ = SET_CURSOR_LINE1;
                        charIndex_ = 0;
                    } else {
                        state_ = COMPLETE;
                    }
                }
                return true;
            }

            case SET_CURSOR_LINE1: {
                // Check spacing
                uint32_t now = millis();
                if (now - lastCharSendTime_ < MIN_CHAR_INTERVAL_MS) {
                    return true;  // Not ready yet, try again next loop
                }

                // Send setCursor command (~0.5ms blocking @ 400kHz)
                lcd_->setCursor(0, 1);
                lastCharSendTime_ = now;
                state_ = PRINT_LINE1_CHAR;
                charIndex_ = 0;
                return true;
            }

            case PRINT_LINE1_CHAR: {
                // Check spacing - ensure 3ms between characters
                uint32_t now = millis();
                if (now - lastCharSendTime_ < MIN_CHAR_INTERVAL_MS) {
                    return true;  // Not ready yet, try again next loop
                }

                // Send ONE character (~0.5ms blocking @ 400kHz)
                lcd_->write(pendingLine1_[charIndex_]);
                lastCharSendTime_ = now;

                charIndex_++;
                if (charIndex_ >= 16) {
                    // Line 1 complete - copy to current buffer
                    memcpy(line1_, pendingLine1_, 16);
                    line1NeedsUpdate_ = false;
                    state_ = COMPLETE;
                }
                return true;
            }

            case COMPLETE: {
                // Update finished, return to idle
                state_ = IDLE;
                return false;
            }

            default:
                state_ = IDLE;
                return false;
        }
    }

    /**
     * Check if update is in progress
     */
    bool isUpdating() const {
        return state_ != IDLE;
    }

    /**
     * Get current content of a line (what's actually on the LCD)
     * @param line - Line number (0 or 1)
     * @return Pointer to line content (null-terminated, 16 chars + null)
     */
    const char* getCurrentLine(uint8_t line) const {
        return (line == 0) ? line0_ : line1_;
    }

    /**
     * Immediately complete any in-progress update (for emergency situations)
     * WARNING: This will block! Only use for screen transitions.
     */
    void finishUpdate() {
        while (update()) {
            // Keep calling update() until complete
        }
    }

    /**
     * Reset state and clear LCD (blocking operation)
     * Use only for screen transitions
     */
    void reset() {
        lcd_->clear();

        memset(line0_, ' ', 16);
        line0_[16] = '\0';
        memset(line1_, ' ', 16);
        line1_[16] = '\0';
        memset(pendingLine0_, ' ', 16);
        pendingLine0_[16] = '\0';
        memset(pendingLine1_, ' ', 16);
        pendingLine1_[16] = '\0';

        state_ = IDLE;
        charIndex_ = 0;
        line0NeedsUpdate_ = false;
        line1NeedsUpdate_ = false;
        lastCharSendTime_ = 0;
    }

    /**
     * Get debug stats
     */
    void printStats() const {
        Serial.println("=== LCDManager Stats ===");
        Serial.printf("State: %d\n", state_);
        Serial.printf("Char index: %d/16\n", charIndex_);
        Serial.printf("Line 0 needs update: %s\n", line0NeedsUpdate_ ? "YES" : "NO");
        Serial.printf("Line 1 needs update: %s\n", line1NeedsUpdate_ ? "YES" : "NO");
        Serial.printf("Last char send: %lu ms ago\n", millis() - lastCharSendTime_);
        Serial.printf("Line 0: '%s'\n", line0_);
        Serial.printf("Line 1: '%s'\n", line1_);
        Serial.println("=======================");
    }

    // Deprecated methods (kept for compatibility, do nothing now)
    void setPlaybackMode(bool isPlaying) { (void)isPlaying; }
    void setThrottleInterval(uint32_t intervalMs) { (void)intervalMs; }
    uint32_t getThrottleInterval() const { return 0; }
    bool isDirty() const { return line0NeedsUpdate_ || line1NeedsUpdate_; }
    bool isPlaybackMode() const { return false; }
    void forceNextUpdate() {}
    bool hasScheduledUpdate() const { return false; }
    uint32_t getTimeUntilNextUpdate() const { return 0; }
    void forceImmediateUpdate() { finishUpdate(); }
};

#endif // LCD_MANAGER_H
