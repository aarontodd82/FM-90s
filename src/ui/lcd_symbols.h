#ifndef LCD_SYMBOLS_H
#define LCD_SYMBOLS_H

#include <Adafruit_RGBLCDShield.h>

/**
 * LCD Custom Character Definitions for Adafruit RGB LCD Shield
 *
 * The shield allows 8 custom characters (slots 0-7).
 * Each character is 5x8 pixels.
 *
 * Usage:
 *   LCDSymbols::init(lcd);
 *   lcd->print("Press ");
 *   lcd->write(LCD_CHAR_SELECT);  // Shows filled circle
 */

// Custom character slot assignments
#define LCD_CHAR_UP_ARROW     0  // ↑
#define LCD_CHAR_DOWN_ARROW   1  // ↓
#define LCD_CHAR_LEFT_ARROW   2  // ←
#define LCD_CHAR_RIGHT_ARROW  3  // →
#define LCD_CHAR_SELECT       4  // ● (filled circle)
#define LCD_CHAR_MUSIC        5  // ♪ (music note)
#define LCD_CHAR_FOLDER       6  // Folder icon
#define LCD_CHAR_PLAY         7  // ► (play triangle)

class LCDSymbols {
public:
    // Initialize custom characters on the LCD
    static void init(Adafruit_RGBLCDShield* lcd) {
        if (!lcd) return;

        // Up Arrow (↑)
        static const uint8_t upArrow[8] = {
            0b00100,
            0b01110,
            0b10101,
            0b00100,
            0b00100,
            0b00100,
            0b00100,
            0b00000
        };

        // Down Arrow (↓)
        static const uint8_t downArrow[8] = {
            0b00100,
            0b00100,
            0b00100,
            0b00100,
            0b10101,
            0b01110,
            0b00100,
            0b00000
        };

        // Left Arrow (←)
        static const uint8_t leftArrow[8] = {
            0b00000,
            0b00100,
            0b01000,
            0b11111,
            0b01000,
            0b00100,
            0b00000,
            0b00000
        };

        // Right Arrow (→)
        static const uint8_t rightArrow[8] = {
            0b00000,
            0b00100,
            0b00010,
            0b11111,
            0b00010,
            0b00100,
            0b00000,
            0b00000
        };

        // Filled Circle (●) for SELECT button
        static const uint8_t selectCircle[8] = {
            0b00000,
            0b01110,
            0b11111,
            0b11111,
            0b11111,
            0b01110,
            0b00000,
            0b00000
        };

        // Music Note (♪)
        static const uint8_t musicNote[8] = {
            0b00011,
            0b00011,
            0b00011,
            0b00011,
            0b01011,
            0b11011,
            0b11000,
            0b00000
        };

        // Folder Icon
        static const uint8_t folder[8] = {
            0b00000,
            0b11100,
            0b11111,
            0b10001,
            0b10001,
            0b11111,
            0b00000,
            0b00000
        };

        // Play Triangle (►)
        static const uint8_t playTriangle[8] = {
            0b00000,
            0b10000,
            0b11000,
            0b11100,
            0b11000,
            0b10000,
            0b00000,
            0b00000
        };

        // Load all custom characters into LCD
        lcd->createChar(LCD_CHAR_UP_ARROW, (uint8_t*)upArrow);
        lcd->createChar(LCD_CHAR_DOWN_ARROW, (uint8_t*)downArrow);
        lcd->createChar(LCD_CHAR_LEFT_ARROW, (uint8_t*)leftArrow);
        lcd->createChar(LCD_CHAR_RIGHT_ARROW, (uint8_t*)rightArrow);
        lcd->createChar(LCD_CHAR_SELECT, (uint8_t*)selectCircle);
        lcd->createChar(LCD_CHAR_MUSIC, (uint8_t*)musicNote);
        lcd->createChar(LCD_CHAR_FOLDER, (uint8_t*)folder);
        lcd->createChar(LCD_CHAR_PLAY, (uint8_t*)playTriangle);
    }

    // Helper to write action legend in standard format
    // Example: writeActionLegend(lcd, true, true, true, "Open")
    //   Result: "↕Nav ←→Act ●Open"
    static void writeActionLegend(Adafruit_RGBLCDShield* lcd,
                                   bool showNav,      // Show ↕Nav?
                                   bool showCycle,    // Show ←→Act?
                                   bool showSelect,   // Show ●[action]?
                                   const char* selectAction = "OK") {
        if (!lcd) return;

        String legend = "";

        if (showNav) {
            lcd->write(LCD_CHAR_UP_ARROW);
            lcd->write(LCD_CHAR_DOWN_ARROW);
            legend += "Nav ";
        }

        if (showCycle) {
            lcd->write(LCD_CHAR_LEFT_ARROW);
            lcd->write(LCD_CHAR_RIGHT_ARROW);
            legend += "Act ";
        }

        if (showSelect) {
            lcd->write(LCD_CHAR_SELECT);
            lcd->print(selectAction);
        }
    }
};

#endif // LCD_SYMBOLS_H
