#ifndef PLAYBACK_NAVIGATION_HANDLER_H
#define PLAYBACK_NAVIGATION_HANDLER_H

#include "event_manager.h"
#include "screen_context.h"
#include "../screen_id.h"  // For ScreenID enum

// Forward declarations
class ScreenManager;
class PlaybackCoordinator;

/**
 * PlaybackNavigationHandler - Centralized navigation decisions for playback lifecycle
 *
 * PURPOSE:
 * Move all playback-related navigation logic out of screens and into a single
 * event-driven handler. Screens forward user actions to PlaybackCoordinator,
 * coordinator orchestrates operations, and this handler makes navigation decisions
 * based on coordinator events.
 *
 * RESPONSIBILITIES:
 * - Navigate to Now Playing screen when file is ready
 * - Navigate away from Now Playing when playback stops
 * - Context-aware decisions (stop reason, current screen, playlist mode)
 * - Show notifications for errors
 * - Future: Handle playlist auto-play
 *
 * DOES NOT:
 * - Control playback (PlaybackCoordinator does that)
 * - Manage audio (PlayerManager does that)
 * - Implement UI (Screens do that)
 *
 * EVENT-DRIVEN FLOW:
 * 1. User presses play → Screen calls coordinator.requestPlay()
 * 2. Coordinator loads file → Fires EVENT_READY_FOR_DISPLAY
 * 3. Handler receives event → Navigates to NOW_PLAYING screen
 * 4. Screen draws → Fires EVENT_SCREEN_READY
 * 5. Coordinator starts playback → Fires EVENT_PLAYBACK_STARTED
 * 6. Song ends OR user stops → Coordinator fires EVENT_PLAYBACK_STOPPED_COMPLETE
 * 7. Handler receives event → Decides where to navigate based on context
 *
 * CONTEXT-AWARE NAVIGATION:
 * - StopReason::USER_REQUEST → Navigate back to previous screen
 * - StopReason::NATURAL_COMPLETION → Check playlist, auto-play or navigate back
 * - StopReason::ERROR → Show error, navigate back
 * - StopReason::EXTERNAL_INTERRUPT → Navigate back to safe screen
 *
 * USAGE:
 *   // In main.cpp setup()
 *   PlaybackNavigationHandler::initialize(context, screenManager, coordinator);
 *
 *   // Handler automatically subscribes to events and makes navigation decisions
 *   // No manual calls needed - completely event-driven
 *
 *   // On shutdown
 *   PlaybackNavigationHandler::cleanup();
 */
class PlaybackNavigationHandler {
public:
    /**
     * Initialize and register event handlers
     * @param context Screen context with all dependencies
     * @param screenManager For navigation control
     * @param coordinator For querying coordinator state (future use)
     */
    static void initialize(ScreenContext* context,
                          ScreenManager* screenManager,
                          PlaybackCoordinator* coordinator);

    /**
     * Cleanup and unregister event handlers
     */
    static void cleanup();

    /**
     * Notify that user intentionally navigated away from Now Playing
     * Used to prevent auto-navigating back during queue playback
     */
    static void notifyUserLeftNowPlaying();

    /**
     * Notify that user wants to see Now Playing screen
     * Used when user explicitly plays a song or navigates to Now Playing
     */
    static void notifyUserWantsNowPlaying();

private:
    // ========================================
    // Static Members
    // ========================================

    static ScreenContext* context_;
    static ScreenManager* screenManager_;
    static PlaybackCoordinator* coordinator_;
    static bool userLeftNowPlaying_;  // Track if user intentionally navigated away

    // ========================================
    // Event Handlers (Coordinator Events)
    // ========================================

    /**
     * Handle EVENT_READY_FOR_DISPLAY
     * - File is loaded and ready to play
     * - Decision: Navigate to Now Playing screen (if not already there)
     *
     * @param userData User data from event (unused)
     */
    static void onReadyForDisplay(void* userData);

    /**
     * Handle EVENT_PLAYBACK_STOPPED_COMPLETE
     * - Stop operation fully complete, audio is silent
     * - Decision: Navigate away based on stop reason and context
     *
     * Context checks:
     * - Current screen (only navigate if on Now Playing)
     * - Stop reason (user vs natural vs error vs interrupt)
     * - Playlist mode (future: auto-play next track)
     *
     * @param reasonInt Stop reason as int (cast to StopReason)
     * @param userData User data from event (unused)
     */
    static void onPlaybackStoppedComplete(int reasonInt, void* userData);

    /**
     * Handle EVENT_FILE_ERROR
     * - File load or start failed
     * - Decision: Show error notification, don't navigate (coordinator handles state)
     *
     * @param errorMessage Error message string
     * @param userData User data from event (unused)
     */
    static void onFileError(const char* errorMessage, void* userData);

    // ========================================
    // Helper Methods
    // ========================================

    /**
     * Check if we should navigate to Now Playing
     * - Current implementation: Always navigate (unless already there)
     * - Future: Check background play preference
     *
     * @return true if should navigate
     */
    static bool shouldShowNowPlayingScreen();

    /**
     * Determine where to navigate after stop completes
     * - Current: Always go back to previous screen
     * - Future: Check playlist mode, auto-play next
     *
     * @param reason Why playback stopped
     * @return Screen ID to navigate to, or SCREEN_NONE to stay
     */
    static ScreenID getScreenAfterStop(StopReason reason);

    /**
     * Get human-readable stop reason string for logging
     *
     * @param reason Stop reason enum
     * @return String representation
     */
    static const char* getStopReasonName(StopReason reason);
};

#endif // PLAYBACK_NAVIGATION_HANDLER_H
