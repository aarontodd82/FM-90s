#include "event_manager.h"

/**
 * EventManager Implementation
 *
 * Part of GUI Framework Redesign - Phase 1: Event System
 * See docs/GUI_FRAMEWORK_REDESIGN.md for architecture details
 */

// ============================================
// CONSTRUCTOR
// ============================================

EventManager::EventManager() {
    // Initialize all callback slots as inactive
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        callbacks_[i].active = false;
        callbacksInt_[i].active = false;
        callbacksStr_[i].active = false;
    }
}

// ============================================
// REGISTRATION
// ============================================

void EventManager::on(EventType type, EventCallback callback, void* context) {
    if (!callback) {
        // // Serial.println("[EventManager] ERROR: Null callback");
        return;
    }

    int slot = findFreeSlot(callbacks_);
    if (slot < 0) {
        // // Serial.println("[EventManager] ERROR: No free callback slots!");
        // // Serial.println("[EventManager] Increase MAX_CALLBACKS or check for memory leaks");
        return;
    }

    callbacks_[slot].type = type;
    callbacks_[slot].callback = callback;
    callbacks_[slot].context = context;
    callbacks_[slot].active = true;

    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] Registered callback for ");
    // // Serial.print(getEventName(type));
    // // Serial.print(" (slot ");
    // // Serial.print(slot);
    // // Serial.println(")");
    #endif
}

void EventManager::onInt(EventType type, EventCallbackInt callback, void* context) {
    if (!callback) {
        // // Serial.println("[EventManager] ERROR: Null callback");
        return;
    }

    int slot = findFreeSlotInt(callbacksInt_);
    if (slot < 0) {
        // // Serial.println("[EventManager] ERROR: No free int callback slots!");
        return;
    }

    callbacksInt_[slot].type = type;
    callbacksInt_[slot].callback = callback;
    callbacksInt_[slot].context = context;
    callbacksInt_[slot].active = true;

    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] Registered int callback for ");
    // // Serial.println(getEventName(type));
    #endif
}

void EventManager::onStr(EventType type, EventCallbackStr callback, void* context) {
    if (!callback) {
        // // Serial.println("[EventManager] ERROR: Null callback");
        return;
    }

    int slot = findFreeSlotStr(callbacksStr_);
    if (slot < 0) {
        // // Serial.println("[EventManager] ERROR: No free string callback slots!");
        return;
    }

    callbacksStr_[slot].type = type;
    callbacksStr_[slot].callback = callback;
    callbacksStr_[slot].context = context;
    callbacksStr_[slot].active = true;

    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] Registered string callback for ");
    // // Serial.println(getEventName(type));
    #endif
}

// ============================================
// UNREGISTRATION
// ============================================

void EventManager::off(EventType type, void* context) {
    int removedCount = 0;

    // Remove from regular callbacks
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacks_[i].active &&
            callbacks_[i].type == type &&
            callbacks_[i].context == context) {
            callbacks_[i].active = false;
            removedCount++;
        }
    }

    // Remove from int callbacks
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksInt_[i].active &&
            callbacksInt_[i].type == type &&
            callbacksInt_[i].context == context) {
            callbacksInt_[i].active = false;
            removedCount++;
        }
    }

    // Remove from string callbacks
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksStr_[i].active &&
            callbacksStr_[i].type == type &&
            callbacksStr_[i].context == context) {
            callbacksStr_[i].active = false;
            removedCount++;
        }
    }

    #ifdef DEBUG_SERIAL_ENABLED
    if (removedCount > 0) {
        // // Serial.print("[EventManager] Removed ");
        // // Serial.print(removedCount);
        // // Serial.print(" callback(s) for ");
        // // Serial.println(getEventName(type));
    }
    #endif
}

void EventManager::offAll(void* context) {
    if (!context) {
        // // Serial.println("[EventManager] WARNING: offAll() called with null context");
        return;
    }

    int removedCount = 0;

    // Remove all regular callbacks with this context
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacks_[i].active && callbacks_[i].context == context) {
            callbacks_[i].active = false;
            removedCount++;
        }
    }

    // Remove all int callbacks with this context
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksInt_[i].active && callbacksInt_[i].context == context) {
            callbacksInt_[i].active = false;
            removedCount++;
        }
    }

    // Remove all string callbacks with this context
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksStr_[i].active && callbacksStr_[i].context == context) {
            callbacksStr_[i].active = false;
            removedCount++;
        }
    }

    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] Removed ");
    // // Serial.print(removedCount);
    // // Serial.print(" callback(s) for context ");
    // // Serial.println((unsigned long)context, HEX);
    #endif
}

// ============================================
// FIRING EVENTS
// ============================================

void EventManager::fire(EventType type) {
    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] Fire: ");
    // // Serial.println(getEventName(type));
    #endif

    int calledCount = 0;

    // Call all registered callbacks for this event type
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacks_[i].active && callbacks_[i].type == type) {
            if (callbacks_[i].callback) {
                callbacks_[i].callback(callbacks_[i].context);
                calledCount++;
            }
        }
    }

    #ifdef DEBUG_SERIAL_ENABLED
    if (calledCount > 0) {
        // // Serial.print("[EventManager] Called ");
        // // Serial.print(calledCount);
        // // Serial.println(" callback(s)");
    }
    #endif
}

void EventManager::fireInt(EventType type, int value) {
    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] FireInt: ");
    // // Serial.print(getEventName(type));
    // // Serial.print(" (value=");
    // // Serial.print(value);
    // // Serial.println(")");
    #endif

    int calledCount = 0;

    // Call all registered int callbacks for this event type
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksInt_[i].active && callbacksInt_[i].type == type) {
            if (callbacksInt_[i].callback) {
                callbacksInt_[i].callback(value, callbacksInt_[i].context);
                calledCount++;
            }
        }
    }

    #ifdef DEBUG_SERIAL_ENABLED
    if (calledCount > 0) {
        // // Serial.print("[EventManager] Called ");
        // // Serial.print(calledCount);
        // // Serial.println(" callback(s)");
    }
    #endif
}

void EventManager::fireStr(EventType type, const char* message) {
    #ifdef DEBUG_SERIAL_ENABLED
    // // Serial.print("[EventManager] FireStr: ");
    // // Serial.print(getEventName(type));
    // // Serial.print(" (message=\"");
    // // Serial.print(message ? message : "null");
    // // Serial.println("\")");
    #endif

    int calledCount = 0;

    // Call all registered string callbacks for this event type
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacksStr_[i].active && callbacksStr_[i].type == type) {
            if (callbacksStr_[i].callback) {
                callbacksStr_[i].callback(message, callbacksStr_[i].context);
                calledCount++;
            }
        }
    }

    #ifdef DEBUG_SERIAL_ENABLED
    if (calledCount > 0) {
        // // Serial.print("[EventManager] Called ");
        // // Serial.print(calledCount);
        // // Serial.println(" callback(s)");
    }
    #endif
}

// ============================================
// UTILITIES
// ============================================

const char* EventManager::getEventName(EventType type) {
    switch (type) {
        case EVENT_BT_INITIALIZED: return "BT_INITIALIZED";
        case EVENT_BT_CONNECTED: return "BT_CONNECTED";
        case EVENT_BT_DISCONNECTED: return "BT_DISCONNECTED";
        case EVENT_BT_SCAN_STARTED: return "BT_SCAN_STARTED";
        case EVENT_BT_SCAN_COMPLETE: return "BT_SCAN_COMPLETE";
        case EVENT_BT_DEVICE_FOUND: return "BT_DEVICE_FOUND";
        case EVENT_BT_ERROR: return "BT_ERROR";
        case EVENT_USB_CONNECTED: return "USB_CONNECTED";
        case EVENT_USB_DISCONNECTED: return "USB_DISCONNECTED";
        case EVENT_PLAYBACK_STARTED: return "PLAYBACK_STARTED";
        case EVENT_PLAYBACK_STOPPED: return "PLAYBACK_STOPPED";
        case EVENT_PLAYBACK_PAUSED: return "PLAYBACK_PAUSED";
        case EVENT_PLAYBACK_RESUMED: return "PLAYBACK_RESUMED";
        case EVENT_PLAYBACK_POSITION_CHANGED: return "PLAYBACK_POSITION_CHANGED";
        case EVENT_PLAYBACK_LOADING: return "PLAYBACK_LOADING";
        case EVENT_READY_FOR_DISPLAY: return "READY_FOR_DISPLAY";
        case EVENT_SCREEN_READY: return "SCREEN_READY";
        case EVENT_PLAYBACK_STARTING: return "PLAYBACK_STARTING";
        case EVENT_PLAYBACK_STOPPING: return "PLAYBACK_STOPPING";
        case EVENT_PLAYBACK_STOPPED_COMPLETE: return "PLAYBACK_STOPPED_COMPLETE";
        case EVENT_FILE_LOADED: return "FILE_LOADED";
        case EVENT_FILE_ERROR: return "FILE_ERROR";
        case EVENT_FILE_SELECTED: return "FILE_SELECTED";
        case EVENT_PLAYLIST_CREATED: return "PLAYLIST_CREATED";
        case EVENT_PLAYLIST_LOADED: return "PLAYLIST_LOADED";
        case EVENT_PLAYLIST_MODIFIED: return "PLAYLIST_MODIFIED";
        case EVENT_PLAYLIST_ITEM_ADDED: return "PLAYLIST_ITEM_ADDED";
        case EVENT_PLAYLIST_ITEM_REMOVED: return "PLAYLIST_ITEM_REMOVED";
        case EVENT_SETTINGS_CHANGED: return "SETTINGS_CHANGED";
        case EVENT_AUDIO_SETTINGS_CHANGED: return "AUDIO_SETTINGS_CHANGED";
        case EVENT_FLOPPY_TRANSFER_STARTED: return "FLOPPY_TRANSFER_STARTED";
        case EVENT_FLOPPY_TRANSFER_PROGRESS: return "FLOPPY_TRANSFER_PROGRESS";
        case EVENT_FLOPPY_TRANSFER_COMPLETE: return "FLOPPY_TRANSFER_COMPLETE";
        case EVENT_FLOPPY_TRANSFER_FAILED: return "FLOPPY_TRANSFER_FAILED";
        case EVENT_QUEUE_TRACK_ADDED: return "QUEUE_TRACK_ADDED";
        case EVENT_QUEUE_TRACK_REMOVED: return "QUEUE_TRACK_REMOVED";
        case EVENT_QUEUE_CLEARED: return "QUEUE_CLEARED";
        case EVENT_QUEUE_CHANGED: return "QUEUE_CHANGED";
        case EVENT_QUEUE_TRACK_CHANGED: return "QUEUE_TRACK_CHANGED";
        default: return "UNKNOWN_EVENT";
    }
}

int EventManager::getCallbackCount() const {
    int count = 0;

    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacks_[i].active) count++;
        if (callbacksInt_[i].active) count++;
        if (callbacksStr_[i].active) count++;
    }

    return count;
}

// ============================================
// PRIVATE HELPERS
// ============================================

int EventManager::findFreeSlot(CallbackEntry* array) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!array[i].active) {
            return i;
        }
    }
    return -1;  // No free slots
}

int EventManager::findFreeSlotInt(CallbackEntryInt* array) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!array[i].active) {
            return i;
        }
    }
    return -1;
}

int EventManager::findFreeSlotStr(CallbackEntryStr* array) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!array[i].active) {
            return i;
        }
    }
    return -1;
}
