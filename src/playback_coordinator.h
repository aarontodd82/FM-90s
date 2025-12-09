#pragma once

#include <Arduino.h>
#include "ui/framework/event_manager.h"  // For EventManager and StopReason
#include "audio_player_interface.h"      // For FileFormat

// Forward declarations
class PlayerManager;
class PlaybackState;
class QueueManager;

/**
 * CoordinatorState - Playback coordinator state machine states
 *
 * State flow:
 * IDLE → LOADING → READY_TO_DISPLAY → STARTING_PLAYBACK → PLAYING → STOPPING → STOPPED → IDLE
 *
 * Error paths:
 * LOADING → IDLE (load failed)
 * STARTING_PLAYBACK → IDLE (start failed)
 */
enum class CoordinatorState {
    IDLE,                   // No playback activity, ready to accept requests
    LOADING,                // File is being loaded by PlayerManager
    READY_TO_DISPLAY,       // File loaded, waiting for Now Playing screen to draw
    STARTING_PLAYBACK,      // Screen ready, about to start audio
    PLAYING,                // Active playback
    STOPPING,               // Stop operation in progress
    STOPPED                 // Stop complete, safe to navigate (brief transition state)
};

/**
 * PlaybackCoordinator - Central state machine for playback lifecycle
 *
 * PURPOSE:
 * Orchestrates the playback lifecycle using a clean state machine and event-driven
 * communication. Sequences operations in the correct order to prevent race conditions
 * and ensure audio is always silent before screen transitions.
 *
 * RESPONSIBILITIES:
 * - Accept user/system intentions (play, stop, pause, next)
 * - Coordinate PlayerManager operations with proper sequencing
 * - Fire events at each state transition
 * - Handle natural playback completion
 * - Support future playlist functionality
 *
 * DOES NOT:
 * - Make navigation decisions (PlaybackNavigationHandler does that)
 * - Directly manipulate audio (PlayerManager does that)
 * - Implement player logic (players do that)
 *
 * EVENT-DRIVEN FLOW:
 * 1. Coordinator receives intention (requestPlay, requestStop)
 * 2. Coordinator tells PlayerManager to do work
 * 3. PlayerManager calls completion callback when done
 * 4. Coordinator transitions state and fires event
 * 5. Event handlers (navigation, UI) respond to events
 *
 * USAGE:
 *   PlaybackCoordinator coordinator(playerManager, eventManager, playbackState);
 *
 *   // Register callbacks (main.cpp)
 *   playerManager->setLoadCompleteCallback([&](bool success) {
 *       coordinator.onLoadComplete(success);
 *   });
 *   // ... register other callbacks
 *
 *   // User presses play (screen)
 *   coordinator.requestPlay(filePath);
 *
 *   // Screen finishes drawing
 *   eventManager->fire(EVENT_SCREEN_READY);  // Coordinator listening
 *
 *   // User presses stop (screen)
 *   coordinator.requestStop(StopReason::USER_REQUEST);
 *
 *   // Main loop
 *   coordinator.update();
 */
class PlaybackCoordinator {
public:
    /**
     * Constructor
     * @param playerManager PlayerManager instance (must not be null)
     * @param eventManager EventManager instance (must not be null)
     * @param playbackState PlaybackState singleton (must not be null)
     * @param queueManager QueueManager instance (can be null if queue not used)
     */
    PlaybackCoordinator(PlayerManager* playerManager,
                       EventManager* eventManager,
                       PlaybackState* playbackState,
                       QueueManager* queueManager = nullptr);

    ~PlaybackCoordinator();

    // ========================================
    // User/System Intentions (Public API)
    // ========================================

    /**
     * Request playback of a file
     * - Initiates LOADING state
     * - Calls PlayerManager::prepareFileAsync()
     * - Wait for onLoadComplete() callback
     *
     * @param path File path to play
     */
    void requestPlay(const char* path);

    /**
     * Request stop
     * - Initiates STOPPING state
     * - Calls PlayerManager::stopAsync()
     * - Wait for onStopComplete() callback
     *
     * @param reason Why stop was requested (affects navigation behavior)
     */
    void requestStop(StopReason reason);

    /**
     * Request pause (future)
     */
    void requestPause();

    /**
     * Request resume (future)
     */
    void requestResume();

    /**
     * Request next track
     * - Checks if queue has next track
     * - If yes: stops current, advances queue, plays next
     * - If no: does nothing
     */
    void requestNext();

    // ========================================
    // Completion Callbacks (Called by PlayerManager or EventManager)
    // ========================================

    /**
     * Called by PlayerManager when file load completes
     * - Transitions: LOADING → READY_TO_DISPLAY (success) or IDLE (failure)
     * - Fires: EVENT_READY_FOR_DISPLAY or EVENT_FILE_ERROR
     *
     * @param success true if load succeeded, false if failed
     */
    void onLoadComplete(bool success);

    /**
     * Called when Now Playing screen finishes drawing
     * - Triggered by EVENT_SCREEN_READY
     * - Transitions: READY_TO_DISPLAY → STARTING_PLAYBACK → PLAYING
     * - Calls: PlayerManager::startPlaybackAsync()
     */
    void onScreenReady();

    /**
     * Called by PlayerManager when playback start completes
     * - Transitions: STARTING_PLAYBACK → PLAYING (success) or IDLE (failure)
     * - Fires: EVENT_PLAYBACK_STARTED
     *
     * @param success true if start succeeded, false if failed
     */
    void onStartComplete(bool success);

    /**
     * Called by PlayerManager when stop completes
     * - Transitions: STOPPING → STOPPED → IDLE
     * - Fires: EVENT_PLAYBACK_STOPPED_COMPLETE (with stop reason)
     */
    void onStopComplete();

    /**
     * Called by PlayerManager when song ends naturally
     * - If queue has next: auto-advance to next track
     * - If no queue: calls requestStop(StopReason::NATURAL_COMPLETION)
     * - Coordinator handles stop sequence
     */
    void onNaturalCompletion();

    // ========================================
    // Status Queries
    // ========================================

    /**
     * Get current coordinator state
     */
    CoordinatorState getState() const { return state_; }

    /**
     * Check if coordinator can accept a play request
     * @return true if in IDLE state
     */
    bool canAcceptPlayRequest() const { return state_ == CoordinatorState::IDLE; }

    /**
     * Check if coordinator is busy
     * @return true if not IDLE
     */
    bool isBusy() const { return state_ != CoordinatorState::IDLE; }

    // ========================================
    // Main Loop
    // ========================================

    /**
     * Update coordinator
     * - Call from main loop
     * - Currently minimal (state machine is callback-driven)
     * - Future: timeout detection, async operation monitoring
     */
    void update();

private:
    // ========================================
    // Helper Methods
    // ========================================

    /**
     * Transition to new state and log
     */
    void transitionTo(CoordinatorState newState);

    /**
     * Get state name for logging
     */
    const char* getStateName(CoordinatorState state) const;

    /**
     * Static callback for EVENT_SCREEN_READY
     * Casts userData to PlaybackCoordinator* and calls instance method
     */
    static void onScreenReadyCallback(void* userData);

    // ========================================
    // Member Variables
    // ========================================

    // Dependencies
    PlayerManager* playerManager_;
    EventManager* eventManager_;
    PlaybackState* playbackState_;
    QueueManager* queueManager_;  // Optional - can be null if queue not used

    // State machine
    CoordinatorState state_;
    StopReason stopReason_;

    // Pending operation data
    String pendingFilePath_;
};
