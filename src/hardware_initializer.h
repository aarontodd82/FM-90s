#pragma once
#include <Arduino.h>
#include <Audio.h>
#include "opl3_synth.h"

// Forward declarations
class DisplayManager;
class ScreenManager;
class Adafruit_RGBLCDShield;
class USBDriveManager;
class FileBrowser;
class FloppyManager;
struct USBHost;
struct USBHub;
struct USBDrive;
struct USBFilesystem;

/**
 * HardwareInitializer - Centralizes all hardware initialization in correct order
 *
 * Initialization order matters to prevent:
 * - Screen blinks from repeated initialization
 * - LCD displaying wrong content (e.g., "Ready!" instead of menu)
 * - USB enumeration issues
 * - Audio glitches
 */
class HardwareInitializer {
public:
    struct Config {
        // Serial configuration
        bool enableSerial = true;
        uint32_t serialBaud = 115200;
        uint32_t serialWaitTime = 2000;  // ms to wait for serial connection

        // Display configuration
        bool enableDisplay = true;
        bool showSplashScreen = false;  // Don't show "Ready!" - let screen manager handle display

        // Audio configuration
        bool enableAudioBoard = true;
        float masterVolume = 0.7f;
        float opl3MixLevel = 0.8f;   // 80% to prevent clipping
        float pcmMixLevel = 0.0f;    // Initially silent, FM90S will control
        int lineInLevel = 10;

        // OPL3 configuration
        bool enableOPL3 = true;
        uint8_t max4OpVoices = 6;
        bool force2OpMode = false;

        // Storage configuration
        bool enableSDCard = true;
        bool enableUSBHost = true;
        bool enableFloppy = true;

        // Timing configuration
        uint32_t usbInitDelay = 2000;  // CRITICAL: Delay for USB enumeration
    };

    struct InitResult {
        bool success = true;

        // Subsystem status
        bool serialReady = false;
        bool displayReady = false;
        bool audioReady = false;
        bool opl3Ready = false;
        bool sdCardReady = false;
        bool usbReady = false;
        bool floppyReady = false;

        // Object pointers created during initialization
        DisplayManager* displayManager = nullptr;
        ScreenManager* screenManager = nullptr;
        Adafruit_RGBLCDShield* lcd = nullptr;
        OPL3Synth* opl3 = nullptr;
        USBDriveManager* usbDrive = nullptr;
        FileBrowser* browser = nullptr;
        FloppyManager* floppy = nullptr;

        // Error messages
        String errorMessage;
    };

    /**
     * Initialize all hardware in the correct order
     * This is the main entry point - call this from setup()
     */
    static InitResult initializeAll(const Config& config);

    /**
     * Individual initialization functions (public for testing/debugging)
     */
    static bool initializeSerial(const Config& config);
    static InitResult initializeDisplay(const Config& config);
    static bool initializeAudioBoard(const Config& config,
                                     AudioMixer4& mixerLeft,
                                     AudioMixer4& mixerRight,
                                     AudioControlSGTL5000& audioShield);
    static OPL3Synth* initializeOPL3(const Config& config);
    static bool initializeSDCard();
    static USBDriveManager* initializeUSBHost(FileBrowser* browser,
                                              USBHost& myusb,
                                              USBHub& hub1,
                                              USBDrive& msDrive1,
                                              USBFilesystem& myFS);
    static FloppyManager* initializeFloppy(FileBrowser* browser);

private:
    // Helper functions
    static void configureOPL3Pins(OPL3Pins& pins);
    static void configureAudioMixers(AudioMixer4& mixerLeft,
                                     AudioMixer4& mixerRight,
                                     const Config& config);
    static void printSystemBanner();
    static void printInitStatus(const InitResult& result);
};