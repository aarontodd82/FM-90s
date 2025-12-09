#ifndef SCREEN_CONTEXT_H
#define SCREEN_CONTEXT_H

#include "../../retro_ui.h"
#include "../lcd_manager.h"
#include <Adafruit_RGBLCDShield.h>

// Forward declarations to avoid circular dependencies
class EventManager;
class OPL3Synth;
class FileSource;
class USBDriveManager;
class BluetoothManager;
class FloppyManager;
class PlaybackState;
class ScreenManager;
class PlayerManager;
class PlaybackCoordinator;
class QueueManager;
class StatusBarManager;

/**
 * ScreenContext - Dependency injection container for all screen dependencies
 *
 * Purpose: Provide screens with access to all system services without globals
 *
 * Benefits:
 * - No more global access (g_midiPlayer, etc.)
 * - Clear dependency declaration
 * - Easy to mock for testing
 * - Single place to validate dependencies
 *
 * Usage:
 *   ScreenContext* ctx = new ScreenContext();
 *   ctx->ui = displayManager->getUI();
 *   ctx->lcd = lcdShield;
 *   // ... set all fields
 *   if (!ctx->isValid()) {
 *       Serial.println("ERROR: Invalid context!");
 *   }
 */
struct ScreenContext {
    // ============================================
    // CORE UI COMPONENTS (required)
    // ============================================
    RetroUI* ui;                            // DOS-style character grid rendering
    Adafruit_RGBLCDShield* lcd;            // 16x2 LCD with buttons (raw access)
    LCDManager* lcdManager;                 // Smart LCD update manager (use this instead of lcd directly)

    // ============================================
    // CORE MANAGERS (required)
    // ============================================
    EventManager* eventManager;             // Event system for notifications
    ScreenManager* screenManager;           // Navigation controller (set after creation)

    // ============================================
    // AUDIO SYSTEM (required)
    // ============================================
    OPL3Synth* opl3;                       // FM synthesizer

    // ============================================
    // FILE SYSTEM (required)
    // ============================================
    FileSource* fileSource;                 // Multi-source file abstraction

    // ============================================
    // PLAYBACK STATE (required)
    // ============================================
    PlaybackState* playbackState;           // Global playback tracking

    // ============================================
    // PLAYER MANAGEMENT (required)
    // ============================================
    PlayerManager* playerManager;           // Unified player management (replaces individual players)

    // ============================================
    // PLAYBACK COORDINATION (required - Phase 5)
    // ============================================
    PlaybackCoordinator* coordinator;       // Orchestrates playback lifecycle with event-driven state machine

    // ============================================
    // QUEUE MANAGEMENT (required - Queue System)
    // ============================================
    QueueManager* queueManager;             // Queue system for sequential playback

    // ============================================
    // UI FRAMEWORK (required - GUI Framework)
    // ============================================
    StatusBarManager* statusBarManager;     // Global status bar with "Now:" and "Next:"

    // ============================================
    // OPTIONAL MANAGERS (nullptr if not available)
    // ============================================
    USBDriveManager* usbDrive;             // USB drive hot-plug support
    BluetoothManager* bluetooth;            // ESP32 Bluetooth control
    FloppyManager* floppy;                  // XModem floppy transfers

    // ============================================
    // CONSTRUCTOR
    // ============================================
    ScreenContext()
        : ui(nullptr)
        , lcd(nullptr)
        , lcdManager(nullptr)
        , eventManager(nullptr)
        , screenManager(nullptr)
        , opl3(nullptr)
        , fileSource(nullptr)
        , playbackState(nullptr)
        , playerManager(nullptr)
        , coordinator(nullptr)
        , queueManager(nullptr)
        , statusBarManager(nullptr)
        , usbDrive(nullptr)
        , bluetooth(nullptr)
        , floppy(nullptr)
    {}

    // ============================================
    // VALIDATION
    // ============================================

    /**
     * Check if all required dependencies are set
     * @return true if context is valid and ready to use
     */
    bool isValid() const {
        bool valid = ui != nullptr &&
                     lcd != nullptr &&
                     lcdManager != nullptr &&
                     eventManager != nullptr &&
                     screenManager != nullptr &&
                     opl3 != nullptr &&
                     fileSource != nullptr &&
                     playbackState != nullptr &&
                     playerManager != nullptr &&
                     coordinator != nullptr &&
                     queueManager != nullptr &&
                     statusBarManager != nullptr;

        if (!valid) {
            Serial.println("[ScreenContext] Validation failed! Missing required dependencies:");
            if (!ui) Serial.println("  - ui is nullptr");
            if (!lcd) Serial.println("  - lcd is nullptr");
            if (!lcdManager) Serial.println("  - lcdManager is nullptr");
            if (!eventManager) Serial.println("  - eventManager is nullptr");
            if (!screenManager) Serial.println("  - screenManager is nullptr");
            if (!opl3) Serial.println("  - opl3 is nullptr");
            if (!fileSource) Serial.println("  - fileSource is nullptr");
            if (!playbackState) Serial.println("  - playbackState is nullptr");
            if (!playerManager) Serial.println("  - playerManager is nullptr");
            if (!coordinator) Serial.println("  - coordinator is nullptr");
            if (!queueManager) Serial.println("  - queueManager is nullptr");
            if (!statusBarManager) Serial.println("  - statusBarManager is nullptr");
        }

        return valid;
    }

    /**
     * Check if USB drive is available
     */
    bool hasUSBDrive() const {
        return usbDrive != nullptr;
    }

    /**
     * Check if Bluetooth is available
     */
    bool hasBluetooth() const {
        return bluetooth != nullptr;
    }

    /**
     * Check if Floppy drive is available
     */
    bool hasFloppy() const {
        return floppy != nullptr;
    }
};

#endif // SCREEN_CONTEXT_H
