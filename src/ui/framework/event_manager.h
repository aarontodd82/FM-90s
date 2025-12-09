#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <Arduino.h>
#include "event_types.h"

/**
 * StopReason - Why playback stopped (used with EVENT_PLAYBACK_STOPPED_COMPLETE)
 *
 * This enum allows event handlers to make different decisions based on
 * why playback stopped:
 * - USER_REQUEST: User pressed stop button (navigate away from Now Playing)
 * - NATURAL_COMPLETION: Song ended naturally (check playlist, auto-play next)
 * - ERROR: Error occurred during playback (show error, go back)
 * - EXTERNAL_INTERRUPT: USB disconnect, system event (stop and navigate away)
 */
enum class StopReason {
    USER_REQUEST,         // User pressed stop button
    NATURAL_COMPLETION,   // Song ended naturally
    ERROR,                // Error occurred during playback
    EXTERNAL_INTERRUPT,   // USB disconnect, etc.
    USER_SKIP_NEXT,       // User pressed "Next" button (queue navigation)
    USER_SKIP_PREVIOUS    // User pressed "Previous" button (queue navigation)
};

/**
 * EventManager - Simple callback registry for manager->screen communication
 *
 * Part of GUI Framework Redesign - Phase 1: Event System
 * See docs/GUI_FRAMEWORK_REDESIGN.md for architecture details
 *
 * Features:
 * - Register callbacks for specific event types
 * - Fire events with optional parameters
 * - Automatic cleanup when screens exit
 * - Multiple callbacks per event type supported
 * - Lightweight (no dynamic allocation)
 *
 * Usage Example:
 *   EventManager events;
 *
 *   // Register callback (typically in screen's onEnter())
 *   events.on(EventManager::EVENT_BT_CONNECTED, onBTConnected, this);
 *
 *   // Fire event (in BluetoothManager)
 *   events.fire(EventManager::EVENT_BT_CONNECTED);
 *
 *   // Cleanup (in screen's onExit())
 *   events.offAll(this);
 */
class EventManager {
public:
    static const int MAX_CALLBACKS = 16;

    /**
     * Event Types - Add new events here as needed
     */
    enum EventType {
        // Bluetooth events
        EVENT_BT_INITIALIZED,       // ESP32 Bluetooth subsystem ready
        EVENT_BT_CONNECTED,         // Device connected successfully
        EVENT_BT_DISCONNECTED,      // Device disconnected
        EVENT_BT_SCAN_STARTED,      // Scan started
        EVENT_BT_SCAN_COMPLETE,     // Scan finished (fireInt with device count)
        EVENT_BT_DEVICE_FOUND,      // New device discovered (fireInt with index)
        EVENT_BT_ERROR,             // Error occurred (fireStr with message)

        // USB drive events
        EVENT_USB_CONNECTED,        // USB drive connected
        EVENT_USB_DISCONNECTED,     // USB drive disconnected

        // Playback events (legacy - will be deprecated)
        EVENT_PLAYBACK_STARTED,     // Playback started
        EVENT_PLAYBACK_STOPPED,     // Playback stopped
        EVENT_PLAYBACK_PAUSED,      // Playback paused
        EVENT_PLAYBACK_RESUMED,     // Playback resumed
        EVENT_PLAYBACK_POSITION_CHANGED, // Playback position updated (fireInt with percentage)

        // Playback Coordinator events (new event-driven architecture)
        EVENT_PLAYBACK_LOADING,          // File is being loaded
        EVENT_READY_FOR_DISPLAY,         // File loaded, ready to show Now Playing screen
        EVENT_SCREEN_READY,              // Screen finished drawing, ready to start audio
        EVENT_PLAYBACK_STARTING,         // About to start audio playback
        EVENT_PLAYBACK_STOPPING,         // Stop operation in progress
        EVENT_PLAYBACK_STOPPED_COMPLETE, // Stop fully complete, safe to navigate (fireInt with StopReason)

        // File system events
        EVENT_FILE_LOADED,          // File loaded successfully
        EVENT_FILE_ERROR,           // File load error (fireStr with message)
        EVENT_FILE_SELECTED,        // File selected in browser (fireStr with path)

        // Playlist events
        EVENT_PLAYLIST_CREATED,     // New playlist created (fireStr with name)
        EVENT_PLAYLIST_LOADED,      // Playlist loaded (fireStr with name)
        EVENT_PLAYLIST_MODIFIED,    // Playlist contents changed
        EVENT_PLAYLIST_ITEM_ADDED,  // Item added to playlist (fireInt with index)
        EVENT_PLAYLIST_ITEM_REMOVED,// Item removed from playlist (fireInt with index)

        // Settings events
        EVENT_SETTINGS_CHANGED,     // Settings modified (generic)
        EVENT_AUDIO_SETTINGS_CHANGED, // Audio settings changed (drum sampler, reverb, etc.)

        // Floppy events
        EVENT_FLOPPY_TRANSFER_STARTED,   // XModem transfer started
        EVENT_FLOPPY_TRANSFER_PROGRESS,  // Transfer progress (fireInt with percentage)
        EVENT_FLOPPY_TRANSFER_COMPLETE,  // Transfer completed
        EVENT_FLOPPY_TRANSFER_FAILED,    // Transfer failed (fireStr with error)

        // Queue events
        EVENT_QUEUE_TRACK_ADDED,         // Track added to queue
        EVENT_QUEUE_TRACK_REMOVED,       // Track removed from queue (fireInt with index)
        EVENT_QUEUE_CLEARED,             // Queue cleared
        EVENT_QUEUE_CHANGED,             // Generic queue change (for UI refresh)
        EVENT_QUEUE_TRACK_CHANGED,       // Current track changed (fireInt with new index)

        EVENT_TYPE_COUNT            // Total number of event types
    };

    EventManager();

    // ============================================
    // REGISTRATION
    // ============================================

    /**
     * Register callback for an event (no parameters)
     * @param type - Event type to listen for
     * @param callback - Function to call when event fires
     * @param context - User data (typically 'this' pointer)
     */
    void on(EventType type, EventCallback callback, void* context);

    /**
     * Register callback for an event with integer parameter
     * @param type - Event type to listen for
     * @param callback - Function to call when event fires
     * @param context - User data
     */
    void onInt(EventType type, EventCallbackInt callback, void* context);

    /**
     * Register callback for an event with string parameter
     * @param type - Event type to listen for
     * @param callback - Function to call when event fires
     * @param context - User data
     */
    void onStr(EventType type, EventCallbackStr callback, void* context);

    // ============================================
    // UNREGISTRATION
    // ============================================

    /**
     * Unregister a specific callback for an event type
     * @param type - Event type
     * @param context - Context to match (removes all callbacks with this context for this type)
     */
    void off(EventType type, void* context);

    /**
     * Unregister ALL callbacks for a specific context
     * Use this in onExit() to clean up all event handlers for a screen
     * @param context - Context to remove (typically 'this')
     */
    void offAll(void* context);

    // ============================================
    // FIRING EVENTS
    // ============================================

    /**
     * Fire an event with no parameters
     * @param type - Event type to fire
     */
    void fire(EventType type);

    /**
     * Fire an event with integer parameter
     * @param type - Event type to fire
     * @param value - Integer value to pass to callbacks
     */
    void fireInt(EventType type, int value);

    /**
     * Fire an event with string parameter
     * @param type - Event type to fire
     * @param message - String message to pass to callbacks
     */
    void fireStr(EventType type, const char* message);

    // ============================================
    // UTILITIES
    // ============================================

    /**
     * Get human-readable event name for debugging
     * @param type - Event type
     * @return Event name string
     */
    static const char* getEventName(EventType type);

    /**
     * Get count of registered callbacks (for debugging)
     * @return Number of active callbacks
     */
    int getCallbackCount() const;

private:
    // Callback storage structures
    struct CallbackEntry {
        EventType type;
        EventCallback callback;
        void* context;
        bool active;  // false = slot available for reuse
    };

    struct CallbackEntryInt {
        EventType type;
        EventCallbackInt callback;
        void* context;
        bool active;
    };

    struct CallbackEntryStr {
        EventType type;
        EventCallbackStr callback;
        void* context;
        bool active;
    };

    // Callback storage arrays
    CallbackEntry callbacks_[MAX_CALLBACKS];
    CallbackEntryInt callbacksInt_[MAX_CALLBACKS];
    CallbackEntryStr callbacksStr_[MAX_CALLBACKS];

    // Helper methods
    int findFreeSlot(CallbackEntry* array);
    int findFreeSlotInt(CallbackEntryInt* array);
    int findFreeSlotStr(CallbackEntryStr* array);
};

#endif // EVENT_MANAGER_H
