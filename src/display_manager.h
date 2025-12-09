#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "RA8875_SPI1.h"
#include <Adafruit_RGBLCDShield.h>
#include "retro_ui.h"

// Display pins - No conflicts with OPL3 or Audio Board
#define RA8875_CS     28
#define RA8875_RESET  29

// SPI1 pins for RA8875 (LPSPI3 on Teensy 4.1)
#define SPI1_MOSI     26   // LPSPI3_SDO
#define SPI1_MISO     39   // LPSPI3_SDI (alt: pin 1)
#define SPI1_SCK      27   // LPSPI3_SCK

class DisplayManager {
private:
    static DisplayManager* instance;
    RA8875_SPI1* tft;
    Adafruit_RGBLCDShield* lcd;
    RetroUI* retroUI;
    bool initialized;

    // Private constructor for singleton
    DisplayManager() : tft(nullptr), lcd(nullptr), retroUI(nullptr), initialized(false) {}

public:
    static DisplayManager* getInstance() {
        if (!instance) {
            instance = new DisplayManager();
        }
        return instance;
    }

    bool begin() {
        // Configure SPI1 pins (CRITICAL: before begin())
        SPI1.setMOSI(SPI1_MOSI);
        SPI1.setMISO(SPI1_MISO);
        SPI1.setSCK(SPI1_SCK);
        SPI1.begin();

        // Create TFT object (CRITICAL: use pointer, not global object)
        tft = new RA8875_SPI1(RA8875_CS, RA8875_RESET, &SPI1);

        // Initialize TFT display
        if (!tft->begin(RA8875_800x480)) {
            // // Serial.println("[DisplayManager] RA8875 init failed!");
            return false;
        }

        // // Serial.println("[DisplayManager] RA8875 initialized successfully");

        // Enable display and backlight
        tft->displayOn(true);
        tft->GPIOX(true);  // Enable TFT output
        tft->PWM1config(true, RA8875_PWM_CLK_DIV1024);
        tft->PWM1out(255);  // Full brightness

        // Initialize LCD Shield (I2C on pins 18/19)
        lcd = new Adafruit_RGBLCDShield();
        lcd->begin(16, 2);

        // Speed up I2C to reduce blocking during LCD updates
        // Default: 100kHz (standard mode)
        // Fast Mode: 400kHz (4x faster, 75% less blocking)
        // Fast Mode Plus: 1MHz (10x faster, 90% less blocking)
        // MCP23017 and Teensy 4.1 both support up to 1.7MHz, but start conservative
        Wire.setClock(400000);  // 400kHz - proven safe for MCP23017
        Serial.println("[DisplayManager] I2C speed set to 400kHz (Fast Mode)");

        lcd->setBacklight(0x7); // White backlight

        // Initialize custom characters for LCD (arrows, symbols, etc.)
        #include "ui/lcd_symbols.h"
        LCDSymbols::init(lcd);

        // // Serial.println("[DisplayManager] LCD Shield initialized successfully");

        // Create RetroUI instance
        retroUI = new RetroUI(tft);
        // // Serial.println("[DisplayManager] RetroUI initialized successfully");

        initialized = true;
        return true;
    }

    // Accessor methods
    RA8875_SPI1* getTFT() { return tft; }
    Adafruit_RGBLCDShield* getLCD() { return lcd; }
    RetroUI* getRetroUI() { return retroUI; }
    bool isInitialized() { return initialized; }

    // Display test pattern using basic RA8875 functions
    void showBasicTestPattern() {
        if (!initialized) return;

        // TFT test pattern
        tft->fillScreen(RA8875_BLUE);
        tft->textMode();
        tft->textSetCursor(10, 10);
        tft->textColor(RA8875_WHITE, RA8875_BLUE);
        tft->textEnlarge(2);  // 3x size
        tft->textWrite("OPL3 MIDI Player");

        tft->textSetCursor(10, 60);
        tft->textEnlarge(1);  // 2x size
        tft->textWrite("Display System Initialized");

        tft->textSetCursor(10, 100);
        tft->textEnlarge(0);  // Normal size
        tft->textWrite("RA8875 800x480 TFT + 16x2 LCD Shield");

        // Return to graphics mode for future drawing
        tft->graphicsMode();

        // LCD test pattern
        lcd->clear();
        lcd->setCursor(0, 0);
        lcd->print("MIDI Player v1.0");
        lcd->setCursor(0, 1);
        lcd->print("System Ready");
    }

    // Display test pattern using RetroUI
    void showRetroUITestPattern() {
        if (!initialized || !retroUI) return;

        // Clear screen with DOS blue
        retroUI->clear(DOS_BLUE);

        // Draw main window
        retroUI->drawWindow(0, 0, 100, 30, " OPL3 MIDI PLAYER v1.0 ",
                          DOS_WHITE, DOS_BLUE);

        // Draw a test panel
        retroUI->drawPanel(5, 3, 40, 10, " Test Panel ", DOS_WHITE, DOS_BLUE);

        // Draw some sample text
        retroUI->drawText(7, 5, "RetroUI Test Successful!", DOS_YELLOW, DOS_BLUE);
        retroUI->drawText(7, 6, "DOS-style UI ready", DOS_WHITE, DOS_BLUE);

        // Draw a menu list
        retroUI->drawPanel(50, 3, 45, 15, " Sample Menu ", DOS_WHITE, DOS_BLUE);
        retroUI->drawListItem(52, 5, 40, "Playlists", true,
                            DOS_WHITE, DOS_BLUE, DOS_BLACK, DOS_CYAN);
        retroUI->drawListItem(52, 7, 40, "SD Card", false);
        retroUI->drawListItem(52, 9, 40, "USB Drive", false);
        retroUI->drawListItem(52, 11, 40, "Settings", false);

        // Draw a progress bar
        retroUI->drawText(5, 15, "Loading:", DOS_WHITE, DOS_BLUE);
        retroUI->drawProgressBar(15, 15, 30, 0.65f, DOS_BRIGHT_GREEN, DOS_BLUE);
        retroUI->drawText(46, 15, "65%", DOS_BRIGHT_GREEN, DOS_BLUE);

        // Draw status bar
        retroUI->drawStatusBar("UP/DOWN=Navigate  SELECT=Open", "Voice: 2-Op:18  4-Op:3");

        // Update LCD
        lcd->clear();
        lcd->setCursor(0, 0);
        lcd->print("RetroUI Active");
        lcd->setCursor(0, 1);
        lcd->print("Test Complete!");
    }

    // Combined test pattern
    void showTestPattern() {
        // Show RetroUI test pattern if available
        if (retroUI) {
            showRetroUITestPattern();
        } else {
            showBasicTestPattern();
        }
    }

    // Clear both displays
    void clear() {
        if (!initialized) return;

        tft->fillScreen(RA8875_BLACK);
        lcd->clear();
    }
};

// Initialize static member (inline to prevent multiple definition errors)
inline DisplayManager* DisplayManager::instance = nullptr;

#endif // DISPLAY_MANAGER_H