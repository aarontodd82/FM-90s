#include "hardware_initializer.h"
#include <SD.h>
#include <Audio.h>
#include <Adafruit_RGBLCDShield.h>
#include "display_manager.h"
#include "opl3_synth.h"
#include "file_browser.h"
#include "floppy_manager.h"
#include "usb_drive_manager.h"
#include "audio_system.h"  // For audio control
#include "audio_globals.h" // For audio object access
#include <USBHost_t36.h>

// Forward declaration - we'll get the actual ScreenManager from display_manager
class ScreenManager;

HardwareInitializer::InitResult HardwareInitializer::initializeAll(const Config& config) {
    InitResult result;

    // ========================================
    // STEP 1: CRITICAL USB INITIALIZATION DELAY
    // ========================================
    // This MUST be first to prevent Teensy bricking!
    delay(config.usbInitDelay);

    // ========================================
    // STEP 2: Serial Port
    // ========================================
    if (config.enableSerial) {
        result.serialReady = initializeSerial(config);
        if (result.serialReady) {
            printSystemBanner();
        }
    }

    // ========================================
    // STEP 3: Display System (Before other visual feedback)
    // ========================================
    if (config.enableDisplay) {
        // if (result.serialReady) // Serial.println("\nInitializing Dual Display System...");

        InitResult displayResult = initializeDisplay(config);
        result.displayReady = displayResult.displayReady;
        result.displayManager = displayResult.displayManager;
        result.screenManager = displayResult.screenManager;
        result.lcd = displayResult.lcd;

        if (!result.displayReady) {
            if (result.serialReady) {
                // // Serial.println("WARNING: Display system initialization failed!");
                // // Serial.println("Check connections:");
                // // Serial.println("  - RA8875: CS=28, RST=29, MOSI=26, MISO=39, SCK=27");
                // // Serial.println("  - LCD Shield: I2C on pins 18/19");
                // // Serial.println("System will continue without displays.");
            }
        } else {
            if (result.serialReady) {
                // // Serial.println("Dual Display System initialized successfully!");
                // // Serial.println("  - RA8875 800x480 TFT on SPI1");
                // // Serial.println("  - RGB LCD Shield on I2C");
            }
        }
    }

    // ========================================
    // STEP 4: Audio Board (Before OPL3 which needs audio)
    // ========================================
    if (config.enableAudioBoard) {
        // if (result.serialReady) // Serial.println("\nInitializing Teensy Audio Board...");

        // Note: These audio objects must be passed in from main.cpp
        // We'll handle this in the main.cpp refactoring
        // For now, we'll just document the requirement
        result.audioReady = true;  // Will be set by actual implementation

        if (result.audioReady && result.serialReady) {
            // // Serial.println("Audio Board initialized - OPL3 Line In passthrough active");
        }
    }

    // ========================================
    // STEP 5: OPL3 Synthesizer
    // ========================================
    if (config.enableOPL3) {
        // if (result.serialReady) // Serial.println("\nConfiguring OPL3 Duo!...");

        result.opl3 = initializeOPL3(config);
        result.opl3Ready = (result.opl3 != nullptr);

        if (result.opl3Ready) {
            if (result.serialReady) {
                // // Serial.println("OPL3 Duo! initialized.");
                if (config.force2OpMode) {
                    // // Serial.println("Voice mode: 2-op ONLY (4-op disabled)");
                } else {
                    // // Serial.print("Voice mode: 2-op + 4-op (max ");
                    // // Serial.print(config.max4OpVoices);
                    // // Serial.println(" concurrent 4-op voices)");
                }
            }
        } else {
            if (result.serialReady) {
                // // Serial.println("ERROR: OPL3 initialization failed!");
            }
            result.success = false;
            result.errorMessage = "OPL3 initialization failed";
        }
    }

    // ========================================
    // STEP 6: SD Card
    // ========================================
    if (config.enableSDCard) {
        // if (result.serialReady) // Serial.println("\nInitializing SD card...");

        result.sdCardReady = initializeSDCard();

        if (!result.sdCardReady) {
            if (result.serialReady) {
                // // Serial.println("ERROR: SD card initialization failed!");
                // // Serial.println("Please check:");
                // // Serial.println("  1. SD card is inserted");
                // // Serial.println("  2. Card is formatted as FAT32");
                // // Serial.println("  3. Card is not damaged");
            }
            // SD card is critical - fail initialization
            result.success = false;
            result.errorMessage = "SD card initialization failed";
            return result;
        }

        if (result.serialReady) {
            // // Serial.println("SD card ready.");
        }
    }

    // ========================================
    // STEP 7: File Browser (needed by USB and Floppy)
    // ========================================
    result.browser = new FileBrowser();

    // ========================================
    // STEP 8: Floppy Manager
    // ========================================
    if (config.enableFloppy) {
        // if (result.serialReady) // Serial.println("\nInitializing Floppy Manager...");

        result.floppy = initializeFloppy(result.browser);
        result.floppyReady = (result.floppy != nullptr);

        if (result.floppyReady) {
            result.floppy->begin();
            // if (result.serialReady) // Serial.println("Floppy Manager ready.");
        }
    }

    // ========================================
    // STEP 9: USB Host (Last, as it can take time to enumerate)
    // ========================================
    // Note: USB objects must be global and initialized before main()
    // They will be passed in from main.cpp
    if (config.enableUSBHost) {
        // if (result.serialReady) // Serial.println("\nInitializing USB Host...");
        // USB initialization will be handled by main.cpp passing in the objects
        // We just mark it as ready for now
        result.usbReady = true;  // Will be set by actual implementation
        // if (result.serialReady) // Serial.println("USB Host ready.");
    }

    // ========================================
    // STEP 10: Final Status
    // ========================================
    if (result.serialReady) {
        // // Serial.println("\n========================================");
        // // Serial.println("Hardware initialization complete!");
        printInitStatus(result);
        // // Serial.println("========================================\n");
    }

    return result;
}

bool HardwareInitializer::initializeSerial(const Config& config) {
    Serial.begin(config.serialBaud);

    // Wait for serial connection (helpful for debugging)
    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime < config.serialWaitTime)) {
        delay(10);
    }

    // Extra delay for serial monitor to catch up
    if (Serial) {
        delay(100);
        return true;
    }

    return false;
}

HardwareInitializer::InitResult HardwareInitializer::initializeDisplay(const Config& config) {
    InitResult result;

    // Initialize display manager
    result.displayManager = DisplayManager::getInstance();
    if (!result.displayManager->begin()) {
        result.displayReady = false;
        return result;
    }

    // Get LCD pointer for compatibility
    result.lcd = result.displayManager->getLCD();

    // Screen manager will be initialized in main.cpp to avoid circular dependencies
    // Just mark display as ready
    result.screenManager = nullptr;  // Will be set by main.cpp

    result.displayReady = true;
    return result;
}

bool HardwareInitializer::initializeAudioBoard(const Config& config,
                                               AudioMixer4& mixerLeft,
                                               AudioMixer4& mixerRight,
                                               AudioControlSGTL5000& audioShield) {
    // Allocate audio memory
    AudioMemory(20);

    // Configure audio shield
    audioShield.enable();
    audioShield.inputSelect(AUDIO_INPUT_LINEIN);  // Select input FIRST
    audioShield.lineInLevel(config.lineInLevel);  // Set level AFTER selecting input
    audioShield.volume(config.masterVolume);

    // Configure mixers
    configureAudioMixers(mixerLeft, mixerRight, config);

    return true;
}

OPL3Synth* HardwareInitializer::initializeOPL3(const Config& config) {
    OPL3Pins pins;
    configureOPL3Pins(pins);

    OPL3Synth* opl3 = new OPL3Synth();
    opl3->begin(pins);

    // Configure voice modes
    opl3->setMax4OpVoices(config.max4OpVoices);
    opl3->setForce2OpMode(config.force2OpMode);

    return opl3;
}

bool HardwareInitializer::initializeSDCard() {
    return SD.begin(BUILTIN_SDCARD);
}

USBDriveManager* HardwareInitializer::initializeUSBHost(FileBrowser* browser,
                                                        USBHost& myusb,
                                                        USBHub& hub1,
                                                        USBDrive& msDrive1,
                                                        USBFilesystem& myFS) {
    USBDriveManager* usbDrive = new USBDriveManager(browser, myusb, hub1, msDrive1, myFS);
    usbDrive->begin();
    return usbDrive;
}

FloppyManager* HardwareInitializer::initializeFloppy(FileBrowser* browser) {
    return new FloppyManager(browser);
}

void HardwareInitializer::configureOPL3Pins(OPL3Pins& pins) {
    // OPL3 Duo! pin assignments (updated for Audio Board compatibility)
    pins.latchWR = 6;   // /WR (unchanged)
    pins.resetIC = 5;   // /IC (moved from 7 to avoid Audio Board conflict)
    pins.addrA0  = 2;   // A0 (moved from 10 to avoid Audio Board conflict)
    pins.addrA1  = 3;   // A1 (moved from 9 to avoid Audio Board conflict)
    pins.addrA2  = 4;   // A2 (moved from 8 to avoid Audio Board conflict)
    pins.spiMOSI = 11;  // MOSI (unchanged)
    pins.spiSCK  = 13;  // SCK (unchanged)
}

void HardwareInitializer::configureAudioMixers(AudioMixer4& mixerLeft,
                                              AudioMixer4& mixerRight,
                                              const Config& config) {
    // Configure left mixer
    mixerLeft.gain(0, config.opl3MixLevel);   // OPL3 left
    mixerLeft.gain(1, config.pcmMixLevel);    // PCM left (FM90S will control)
    mixerLeft.gain(2, 0.0f);                  // Unused
    mixerLeft.gain(3, 0.0f);                  // Unused

    // Configure right mixer
    mixerRight.gain(0, config.opl3MixLevel);  // OPL3 right
    mixerRight.gain(1, config.pcmMixLevel);   // PCM right (FM90S will control)
    mixerRight.gain(2, 0.0f);                 // Unused
    mixerRight.gain(3, 0.0f);                 // Unused
}

void HardwareInitializer::printSystemBanner() {
    // // Serial.println("\n================================");
    // // Serial.println("  Teensy OPL3 Duo! MIDI Player");
    // // Serial.println("================================\n");
    // // Serial.println("Initializing hardware...");
}

void HardwareInitializer::printInitStatus(const InitResult& result) {
    // // Serial.println("Subsystem Status:");
    // Serial.print("  Serial:  "); // Serial.println(result.serialReady ? "OK" : "FAIL");
    // Serial.print("  Display: "); // Serial.println(result.displayReady ? "OK" : "SKIP");
    // Serial.print("  Audio:   "); // Serial.println(result.audioReady ? "OK" : "FAIL");
    // Serial.print("  OPL3:    "); // Serial.println(result.opl3Ready ? "OK" : "FAIL");
    // Serial.print("  SD Card: "); // Serial.println(result.sdCardReady ? "OK" : "FAIL");
    // Serial.print("  USB:     "); // Serial.println(result.usbReady ? "OK" : "SKIP");
    // Serial.print("  Floppy:  "); // Serial.println(result.floppyReady ? "OK" : "SKIP");

    if (!result.success) {
        // // Serial.print("\nERROR: ");
        // // Serial.println(result.errorMessage);
    }
}