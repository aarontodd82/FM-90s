#include "playback_navigation_handler.h"
#include "../screen_manager.h"
#include "../../retro_ui.h"
#include "../../dos_colors.h"
#include "../../playback_coordinator.h"
#include "../../queue_manager.h"

/**
 * PlaybackNavigationHandler Implementation
 *
 * Centralizes all playback-related navigation decisions based on
 * PlaybackCoordinator events.
 */

// ========================================
// Static Member Initialization
// ========================================

ScreenContext* PlaybackNavigationHandler::context_ = nullptr;
ScreenManager* PlaybackNavigationHandler::screenManager_ = nullptr;
PlaybackCoordinator* PlaybackNavigationHandler::coordinator_ = nullptr;
bool PlaybackNavigationHandler::userLeftNowPlaying_ = false;

// ========================================
// Initialization / Cleanup
// ========================================

void PlaybackNavigationHandler::initialize(ScreenContext* context,
                                          ScreenManager* screenManager,
                                          PlaybackCoordinator* coordinator) {
    context_ = context;
    screenManager_ = screenManager;
    coordinator_ = coordinator;

    // Validate dependencies
    if (!context) {
        Serial.println("[PlaybackNavHandler] ERROR: Context is null!");
        return;
    }

    if (!context->eventManager) {
        Serial.println("[PlaybackNavHandler] ERROR: EventManager is null!");
        return;
    }

    if (!screenManager) {
        Serial.println("[PlaybackNavHandler] ERROR: ScreenManager is null!");
        return;
    }

    if (!coordinator) {
        Serial.println("[PlaybackNavHandler] ERROR: PlaybackCoordinator is null!");
        return;
    }

    // Register for coordinator events
    context->eventManager->on(EventManager::EVENT_READY_FOR_DISPLAY,
                             onReadyForDisplay,
                             nullptr);

    context->eventManager->onInt(EventManager::EVENT_PLAYBACK_STOPPED_COMPLETE,
                                 onPlaybackStoppedComplete,
                                 nullptr);

    context->eventManager->onStr(EventManager::EVENT_FILE_ERROR,
                                 onFileError,
                                 nullptr);

    Serial.println("[PlaybackNavHandler] Initialized and subscribed to coordinator events");
}

void PlaybackNavigationHandler::cleanup() {
    if (context_ && context_->eventManager) {
        // Unregister all event callbacks
        context_->eventManager->offAll(nullptr);
        Serial.println("[PlaybackNavHandler] Cleaned up event subscriptions");
    }

    context_ = nullptr;
    screenManager_ = nullptr;
    coordinator_ = nullptr;
    userLeftNowPlaying_ = false;
}

void PlaybackNavigationHandler::notifyUserLeftNowPlaying() {
    Serial.println("[PlaybackNavHandler] User left Now Playing screen");
    userLeftNowPlaying_ = true;
}

void PlaybackNavigationHandler::notifyUserWantsNowPlaying() {
    Serial.println("[PlaybackNavHandler] User wants Now Playing screen");
    userLeftNowPlaying_ = false;
}

// ========================================
// Event Handlers
// ========================================

void PlaybackNavigationHandler::onReadyForDisplay(void* userData) {
    (void)userData;  // Unused

    if (!screenManager_) {
        Serial.println("[PlaybackNavHandler] ERROR: onReadyForDisplay() called but screenManager is null");
        return;
    }

    Serial.println("[PlaybackNavHandler] onReadyForDisplay: File loaded, deciding navigation");

    // Check if we should navigate to Now Playing
    if (!shouldShowNowPlayingScreen()) {
        Serial.println("[PlaybackNavHandler] Background playback mode - not navigating, but firing SCREEN_READY");
        // Don't navigate, but fire EVENT_SCREEN_READY so coordinator can start playback
        if (context_ && context_->eventManager) {
            context_->eventManager->fire(EventManager::EVENT_SCREEN_READY);
        }
        return;
    }

    // Check current screen
    ScreenID currentScreen = screenManager_->getCurrentScreenID();
    Serial.printf("[PlaybackNavHandler] Current screen: %d\n", (int)currentScreen);

    // If already on Now Playing, manually fire EVENT_SCREEN_READY
    // (since screen won't re-enter, it won't fire the event automatically)
    if (currentScreen == SCREEN_NOW_PLAYING) {
        Serial.println("[PlaybackNavHandler] Already on Now Playing screen, manually firing EVENT_SCREEN_READY");
        if (context_ && context_->eventManager) {
            context_->eventManager->fire(EventManager::EVENT_SCREEN_READY);
        }
        return;
    }

    // CRITICAL: Use deferred navigation to prevent use-after-free!
    // requestNavigation() is safe to call from event handlers
    Serial.println("[PlaybackNavHandler] Requesting deferred navigation to Now Playing screen");
    screenManager_->requestNavigation(SCREEN_NOW_PLAYING);

    // Show notification
    if (context_ && context_->ui) {
        Serial.println("[PlaybackNavHandler] Showing notification");
        context_->ui->showStatusNotification("Loading...", 1000, DOS_BLACK, DOS_CYAN);
        Serial.println("[PlaybackNavHandler] Notification shown");
    }

    Serial.println("[PlaybackNavHandler] onReadyForDisplay complete");
}

void PlaybackNavigationHandler::onPlaybackStoppedComplete(int reasonInt, void* userData) {
    (void)userData;  // Unused

    if (!screenManager_) {
        Serial.println("[PlaybackNavHandler] ERROR: onPlaybackStoppedComplete() called but screenManager is null");
        return;
    }

    // Convert reason from int
    StopReason reason = static_cast<StopReason>(reasonInt);
    Serial.printf("[PlaybackNavHandler] onPlaybackStoppedComplete: reason=%s (%d)\n",
                 getStopReasonName(reason), reasonInt);

    // Check current screen
    ScreenID currentScreen = screenManager_->getCurrentScreenID();
    Serial.printf("[PlaybackNavHandler] Current screen: %d\n", (int)currentScreen);

    // Only navigate away if currently on Now Playing screen
    if (currentScreen != SCREEN_NOW_PLAYING) {
        Serial.println("[PlaybackNavHandler] Not on Now Playing screen, no navigation needed");
        return;
    }

    // Decide where to navigate based on stop reason
    ScreenID targetScreen = getScreenAfterStop(reason);

    if (targetScreen == SCREEN_NONE) {
        Serial.println("[PlaybackNavHandler] No navigation needed for this stop reason");
        return;
    }

    // Navigate (deferred to prevent use-after-free)
    Serial.printf("[PlaybackNavHandler] Requesting deferred navigation to screen %d\n", (int)targetScreen);
    screenManager_->requestNavigation(targetScreen);

    // Show reason-specific notification
    if (context_ && context_->ui) {
        switch (reason) {
            case StopReason::USER_REQUEST:
                // User stopped - no notification needed (they know)
                break;

            case StopReason::NATURAL_COMPLETION:
                context_->ui->showStatusNotification("Playback complete", 2000,
                                                    DOS_BLACK, DOS_GREEN);
                break;

            case StopReason::ERROR:
                context_->ui->showStatusNotification("Playback error", 3000,
                                                    DOS_BLACK, DOS_RED);
                break;

            case StopReason::EXTERNAL_INTERRUPT:
                context_->ui->showStatusNotification("Playback interrupted", 3000,
                                                    DOS_BLACK, DOS_YELLOW);
                break;

            case StopReason::USER_SKIP_NEXT:
            case StopReason::USER_SKIP_PREVIOUS:
                // Skip operations - no notification needed (seamless transition)
                break;
        }
    }
}

void PlaybackNavigationHandler::onFileError(const char* errorMessage, void* userData) {
    (void)userData;  // Unused

    if (!errorMessage) {
        errorMessage = "Unknown error";
    }

    Serial.printf("[PlaybackNavHandler] onFileError: %s\n", errorMessage);

    // Show error notification
    if (context_ && context_->ui) {
        // Truncate message if too long for status bar
        char shortMessage[50];
        if (strlen(errorMessage) > 45) {
            strncpy(shortMessage, errorMessage, 45);
            shortMessage[45] = '.';
            shortMessage[46] = '.';
            shortMessage[47] = '.';
            shortMessage[48] = '\0';
        } else {
            strcpy(shortMessage, errorMessage);
        }

        context_->ui->showStatusNotification(shortMessage, 5000, DOS_BLACK, DOS_RED);
    }

    // Don't navigate - coordinator has already returned to IDLE state
    // User is still on whatever screen they were on (file browser, etc)
}

// ========================================
// Helper Methods
// ========================================

bool PlaybackNavigationHandler::shouldShowNowPlayingScreen() {
    // If user intentionally left Now Playing (pressed Browse), don't force them back
    // This allows browsing while queue continues in background
    if (userLeftNowPlaying_) {
        Serial.println("[PlaybackNavHandler] User left Now Playing, not navigating back");
        return false;
    }

    // Future enhancement: Check user preference for background playback
    // if (g_backgroundPlaybackEnabled) {
    //     return false;  // Don't navigate, play in background
    // }

    return true;
}

ScreenID PlaybackNavigationHandler::getScreenAfterStop(StopReason reason) {
    // Context-aware navigation based on stop reason

    switch (reason) {
        case StopReason::USER_REQUEST:
            // User explicitly stopped - go back
            return SCREEN_GO_BACK;

        case StopReason::NATURAL_COMPLETION:
            // Song ended naturally with no next track
            // Coordinator already checked queue - if there WAS a next track,
            // it would auto-advance silently without firing this event
            // Getting NATURAL_COMPLETION means: no auto-advance, navigate away
            Serial.println("[PlaybackNavHandler] Natural completion - going back");
            return SCREEN_GO_BACK;

        case StopReason::ERROR:
            // Error during playback - go back (error already shown)
            return SCREEN_GO_BACK;

        case StopReason::EXTERNAL_INTERRUPT:
            // External interrupt (USB disconnect, etc) - go back to safe screen
            return SCREEN_GO_BACK;

        case StopReason::USER_SKIP_NEXT:
        case StopReason::USER_SKIP_PREVIOUS:
            // User skipped tracks - stay on Now Playing screen
            // PlaybackCoordinator/QueueManager already advanced queue
            // Coordinator will automatically start next track
            // NOTE: These stop reasons should NOT trigger navigation
            return SCREEN_NONE;

        default:
            Serial.printf("[PlaybackNavHandler] WARNING: Unknown stop reason %d\n", (int)reason);
            return SCREEN_GO_BACK;
    }
}

const char* PlaybackNavigationHandler::getStopReasonName(StopReason reason) {
    switch (reason) {
        case StopReason::USER_REQUEST:        return "USER_REQUEST";
        case StopReason::NATURAL_COMPLETION:  return "NATURAL_COMPLETION";
        case StopReason::ERROR:               return "ERROR";
        case StopReason::EXTERNAL_INTERRUPT:  return "EXTERNAL_INTERRUPT";
        case StopReason::USER_SKIP_NEXT:      return "USER_SKIP_NEXT";
        case StopReason::USER_SKIP_PREVIOUS:  return "USER_SKIP_PREVIOUS";
        default:                               return "UNKNOWN";
    }
}
