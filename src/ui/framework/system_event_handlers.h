#ifndef SYSTEM_EVENT_HANDLERS_H
#define SYSTEM_EVENT_HANDLERS_H

#include "event_manager.h"
#include "screen_context.h"

// Forward declarations
class ScreenManager;
class RetroUI;

/**
 * USB Event Handler - Handles USB drive connection/disconnection
 *
 * Purpose: Move USB-related business logic out of ScreenManager
 *
 * Responsibilities:
 * - Stop playback when USB drive disconnected (if playing from USB)
 * - Update main menu status
 * - Show notifications
 * - Navigate away from USB browser if drive removed
 *
 * Usage:
 *   USBEventHandler::initialize(context, screenManager);
 *   // Event handler automatically subscribes to USB events
 *   // Will handle events until cleanup() is called
 */
class USBEventHandler {
public:
    /**
     * Initialize and register event handlers
     */
    static void initialize(ScreenContext* context, ScreenManager* screenManager);

    /**
     * Cleanup and unregister event handlers
     */
    static void cleanup();

private:
    static ScreenContext* context_;
    static ScreenManager* screenManager_;

    /**
     * Handle USB connected event
     */
    static void onUSBConnected(void* userData);

    /**
     * Handle USB disconnected event
     */
    static void onUSBDisconnected(void* userData);
};

/**
 * Playback Event Handler - Handles file loading errors
 *
 * Purpose: Show user-facing error notifications for file loading failures
 *
 * Note: Playback navigation is now handled by PlaybackNavigationHandler
 *
 * Responsibilities:
 * - Show error notifications when files fail to load
 *
 * Usage:
 *   PlaybackEventHandler::initialize(context, screenManager);
 */
class PlaybackEventHandler {
public:
    /**
     * Initialize and register event handlers
     */
    static void initialize(ScreenContext* context, ScreenManager* screenManager);

    /**
     * Cleanup and unregister event handlers
     */
    static void cleanup();

private:
    static ScreenContext* context_;
    static ScreenManager* screenManager_;

    /**
     * Handle file load error
     */
    static void onFileError(const char* message, void* userData);
};

/**
 * Audio Event Handler - Handles audio settings changes
 *
 * Purpose: Apply audio settings changes to the audio system
 *
 * Responsibilities:
 * - Apply drum sampler enable/disable changes
 * - Apply crossfeed and reverb enable/disable changes
 * - Update mixer gains and OPL3 synth state
 *
 * Usage:
 *   AudioEventHandler::initialize(context);
 */
class AudioEventHandler {
public:
    /**
     * Initialize and register event handlers
     */
    static void initialize(ScreenContext* context);

    /**
     * Cleanup and unregister event handlers
     */
    static void cleanup();

private:
    static ScreenContext* context_;

    /**
     * Handle audio settings changed event
     */
    static void onAudioSettingsChanged(void* userData);
};

#endif // SYSTEM_EVENT_HANDLERS_H
