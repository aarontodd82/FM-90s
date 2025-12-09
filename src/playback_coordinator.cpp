#include "playback_coordinator.h"
#include "player_manager.h"
#include "playback_state.h"
#include "queue_manager.h"

// ========================================
// Constructor / Destructor
// ========================================

PlaybackCoordinator::PlaybackCoordinator(PlayerManager* playerManager,
                                       EventManager* eventManager,
                                       PlaybackState* playbackState,
                                       QueueManager* queueManager)
    : playerManager_(playerManager)
    , eventManager_(eventManager)
    , playbackState_(playbackState)
    , queueManager_(queueManager)
    , state_(CoordinatorState::IDLE)
    , stopReason_(StopReason::USER_REQUEST)
{
    // Validate dependencies
    if (!playerManager_) {
        Serial.println("[PlaybackCoordinator] ERROR: PlayerManager is null!");
    }
    if (!eventManager_) {
        Serial.println("[PlaybackCoordinator] ERROR: EventManager is null!");
    }
    if (!playbackState_) {
        Serial.println("[PlaybackCoordinator] ERROR: PlaybackState is null!");
    }
    // queueManager is optional, can be null

    // Register for EVENT_SCREEN_READY (fires when Now Playing screen finishes drawing)
    if (eventManager_) {
        eventManager_->on(EventManager::EVENT_SCREEN_READY, onScreenReadyCallback, this);
        Serial.println("[PlaybackCoordinator] Registered for EVENT_SCREEN_READY");
    }

    Serial.println("[PlaybackCoordinator] Created");
}

PlaybackCoordinator::~PlaybackCoordinator() {
    Serial.println("[PlaybackCoordinator] Destroying");

    // If we're in the middle of playback, stop it
    if (state_ != CoordinatorState::IDLE) {
        Serial.println("[PlaybackCoordinator] Stopping active playback during destruction");
        requestStop(StopReason::EXTERNAL_INTERRUPT);
    }
}

// ========================================
// User/System Intentions
// ========================================

void PlaybackCoordinator::requestPlay(const char* path) {
    if (!path) {
        Serial.println("[PlaybackCoordinator] ERROR: requestPlay() called with null path");
        return;
    }

    // If something is currently playing, stop it first
    // This allows playing a new song while one is playing (queue continues after)
    if (state_ == CoordinatorState::PLAYING) {
        Serial.printf("[PlaybackCoordinator] requestPlay: Stopping current playback to play new song: %s\n", path);

        // Store the path we want to play after stopping
        String pathToPlay = String(path);

        // Stop current playback, then play new song in callback
        transitionTo(CoordinatorState::STOPPING);
        eventManager_->fire(EventManager::EVENT_PLAYBACK_STOPPING);

        playerManager_->stopAsync([this, pathToPlay]() {
            Serial.printf("[PlaybackCoordinator] Stop complete, now playing: %s\n", pathToPlay.c_str());

            transitionTo(CoordinatorState::STOPPED);
            transitionTo(CoordinatorState::IDLE);

            // Now play the new song
            requestPlay(pathToPlay.c_str());
        });
        return;
    }

    // Validate state (must be IDLE to proceed)
    if (state_ != CoordinatorState::IDLE) {
        Serial.printf("[PlaybackCoordinator] ERROR: requestPlay() called in state %s, must be IDLE or PLAYING\n",
                     getStateName(state_));
        return;
    }

    Serial.printf("[PlaybackCoordinator] requestPlay: %s\n", path);

    // Store pending file path
    pendingFilePath_ = path;

    // Transition to LOADING state
    transitionTo(CoordinatorState::LOADING);

    // Fire event: We're starting to load
    eventManager_->fire(EventManager::EVENT_PLAYBACK_LOADING);

    // Tell PlayerManager to prepare the file
    // PlayerManager will call onLoadComplete() when done
    playerManager_->prepareFileAsync(path, [this](bool success) {
        this->onLoadComplete(success);
    });
}

void PlaybackCoordinator::requestStop(StopReason reason) {
    // Can only stop if playing or starting
    if (state_ != CoordinatorState::PLAYING && state_ != CoordinatorState::STARTING_PLAYBACK) {
        Serial.printf("[PlaybackCoordinator] requestStop() called in state %s, ignoring\n",
                     getStateName(state_));
        return;
    }

    Serial.printf("[PlaybackCoordinator] requestStop: reason=%d\n", (int)reason);

    // Store stop reason for later event
    stopReason_ = reason;

    // Transition to STOPPING state
    transitionTo(CoordinatorState::STOPPING);

    // Fire event: We're starting to stop
    eventManager_->fire(EventManager::EVENT_PLAYBACK_STOPPING);

    // Tell PlayerManager to stop
    // PlayerManager will call onStopComplete() when done
    playerManager_->stopAsync([this]() {
        this->onStopComplete();
    });
}

void PlaybackCoordinator::requestPause() {
    // TODO: Implement pause support
    Serial.println("[PlaybackCoordinator] requestPause() - Not yet implemented");
}

void PlaybackCoordinator::requestResume() {
    // TODO: Implement resume support
    Serial.println("[PlaybackCoordinator] requestResume() - Not yet implemented");
}

void PlaybackCoordinator::requestNext() {
    // Check if queue is available and has next track
    if (!queueManager_ || !queueManager_->hasNext() || !playbackState_) {
        Serial.println("[PlaybackCoordinator] requestNext() - No next track in queue");
        return;
    }

    // Can only skip if playing
    if (state_ != CoordinatorState::PLAYING) {
        Serial.printf("[PlaybackCoordinator] requestNext() called in state %s, ignoring\n",
                     getStateName(state_));
        return;
    }

    Serial.println("[PlaybackCoordinator] requestNext() - Skipping to next track");

    // EXACT COPY of onNaturalCompletion pattern:
    // 1. Get current track before we advance
    String currentFile = playbackState_->getCurrentFile();

    // 2. Advance queue - returns next track path
    const char* nextTrack = queueManager_->playNext(currentFile.c_str());
    if (!nextTrack) {
        Serial.println("[PlaybackCoordinator] requestNext() - playNext returned null");
        return;
    }

    Serial.printf("[PlaybackCoordinator] requestNext() - Next track: %s\n", nextTrack);

    // 3. Stop with callback that starts next track (same as natural completion)
    transitionTo(CoordinatorState::STOPPING);
    eventManager_->fire(EventManager::EVENT_PLAYBACK_STOPPING);

    // Stop current player with callback to start next
    playerManager_->stopAsync([this, nextTrack]() {
        Serial.printf("[PlaybackCoordinator] Manual skip stop complete, starting next: %s\n", nextTrack);

        // Brief transition through STOPPED and IDLE (EXACT COPY of natural completion)
        transitionTo(CoordinatorState::STOPPED);
        transitionTo(CoordinatorState::IDLE);

        // Start playing next track immediately
        requestPlay(nextTrack);
    });
}


// ========================================
// Completion Callbacks
// ========================================

void PlaybackCoordinator::onLoadComplete(bool success) {
    Serial.printf("[PlaybackCoordinator] onLoadComplete: success=%d, state=%s\n",
                 success, getStateName(state_));

    // Verify we're in expected state
    if (state_ != CoordinatorState::LOADING) {
        Serial.printf("[PlaybackCoordinator] WARNING: onLoadComplete() called in unexpected state %s\n",
                     getStateName(state_));
        return;
    }

    if (!success) {
        // Load failed - return to IDLE
        Serial.println("[PlaybackCoordinator] Load failed, returning to IDLE");
        transitionTo(CoordinatorState::IDLE);

        // Fire error event
        eventManager_->fireStr(EventManager::EVENT_FILE_ERROR, "Failed to load file");
        return;
    }

    // Load succeeded - transition to READY_TO_DISPLAY
    transitionTo(CoordinatorState::READY_TO_DISPLAY);

    // Fire event: File is loaded, ready to show Now Playing screen
    Serial.println("[PlaybackCoordinator] File loaded, firing EVENT_READY_FOR_DISPLAY");
    eventManager_->fire(EventManager::EVENT_READY_FOR_DISPLAY);

    // Navigation handler will receive this event and switch to Now Playing screen
    // When screen finishes drawing, it will fire EVENT_SCREEN_READY
    // which will trigger onScreenReady()
}

void PlaybackCoordinator::onScreenReady() {
    Serial.printf("[PlaybackCoordinator] onScreenReady: state=%s\n", getStateName(state_));

    // Only proceed if we're waiting for screen
    if (state_ != CoordinatorState::READY_TO_DISPLAY) {
        // Screen ready events can fire at other times (screen changes, etc)
        // Just ignore if not in right state
        return;
    }

    Serial.println("[PlaybackCoordinator] Screen ready, starting playback");

    // Transition to STARTING_PLAYBACK
    transitionTo(CoordinatorState::STARTING_PLAYBACK);

    // Fire event: About to start audio
    eventManager_->fire(EventManager::EVENT_PLAYBACK_STARTING);

    // Tell PlayerManager to start playback
    // PlayerManager will call onStartComplete() when done
    playerManager_->startPlaybackAsync([this](bool success) {
        this->onStartComplete(success);
    });
}

void PlaybackCoordinator::onStartComplete(bool success) {
    Serial.printf("[PlaybackCoordinator] onStartComplete: success=%d, state=%s\n",
                 success, getStateName(state_));

    // Verify we're in expected state
    if (state_ != CoordinatorState::STARTING_PLAYBACK) {
        Serial.printf("[PlaybackCoordinator] WARNING: onStartComplete() called in unexpected state %s\n",
                     getStateName(state_));
        return;
    }

    if (!success) {
        // Start failed - return to IDLE
        Serial.println("[PlaybackCoordinator] Start failed, returning to IDLE");
        transitionTo(CoordinatorState::IDLE);

        // Fire error event
        eventManager_->fireStr(EventManager::EVENT_FILE_ERROR, "Failed to start playback");
        return;
    }

    // Small safety margin after PlayerManager reports ready
    delay(5);

    // Start succeeded - transition to PLAYING
    transitionTo(CoordinatorState::PLAYING);

    // Fire event: Playback is active
    Serial.println("[PlaybackCoordinator] Playback started, firing EVENT_PLAYBACK_STARTED");
    eventManager_->fire(EventManager::EVENT_PLAYBACK_STARTED);

    // Now playing! PlayerManager's update() will detect natural completion
    // and call onNaturalCompletion() when song ends
}

void PlaybackCoordinator::onStopComplete() {
    Serial.printf("[PlaybackCoordinator] onStopComplete: state=%s, reason=%d\n",
                 getStateName(state_), (int)stopReason_);

    // Verify we're in expected state
    if (state_ != CoordinatorState::STOPPING) {
        Serial.printf("[PlaybackCoordinator] WARNING: onStopComplete() called in unexpected state %s\n",
                     getStateName(state_));
        return;
    }

    // Small safety margin after PlayerManager reports complete
    delay(5);

    // Transition to STOPPED (brief transition state)
    transitionTo(CoordinatorState::STOPPED);

    // Fire event with stop reason so handlers can decide what to do
    Serial.printf("[PlaybackCoordinator] Stop complete, firing EVENT_PLAYBACK_STOPPED_COMPLETE with reason %d\n",
                 (int)stopReason_);
    eventManager_->fireInt(EventManager::EVENT_PLAYBACK_STOPPED_COMPLETE, (int)stopReason_);

    // Transition to IDLE - ready for next request
    transitionTo(CoordinatorState::IDLE);

    // NOTE: Skip operations (USER_SKIP_NEXT/PREVIOUS) are now handled directly in
    // requestNext()/requestPrevious() using callbacks (same pattern as natural completion).
    // They bypass requestStop() entirely, so this callback only handles explicit user stops.

    // Navigation handler will receive EVENT_PLAYBACK_STOPPED_COMPLETE and
    // decide whether to navigate away based on stop reason and context
}

void PlaybackCoordinator::onNaturalCompletion() {
    Serial.printf("[PlaybackCoordinator] onNaturalCompletion: state=%s\n", getStateName(state_));

    // Check if queue has next track
    if (queueManager_ && queueManager_->hasNext() && playbackState_) {
        Serial.println("[PlaybackCoordinator] Queue has next track, auto-advancing");

        // Get current track before we advance
        String currentFile = playbackState_->getCurrentFile();

        // Advance queue - returns next track path
        const char* nextTrack = queueManager_->playNext(currentFile.c_str());
        if (nextTrack) {
            Serial.printf("[PlaybackCoordinator] Auto-playing next: %s\n", nextTrack);

            // IMPORTANT: During auto-advance, we do NOT fire PLAYBACK_STOPPED_COMPLETE
            // This prevents navigation handler from trying to navigate away
            // We silently transition from one track to the next

            transitionTo(CoordinatorState::STOPPING);
            eventManager_->fire(EventManager::EVENT_PLAYBACK_STOPPING);

            // Stop current player
            playerManager_->stopAsync([this, nextTrack]() {
                // After stop completes, immediately start next track
                Serial.println("[PlaybackCoordinator] Auto-advance: stop complete, starting next track");

                // Brief transition through STOPPED and IDLE
                transitionTo(CoordinatorState::STOPPED);
                transitionTo(CoordinatorState::IDLE);

                // NOTE: We do NOT fire EVENT_PLAYBACK_STOPPED_COMPLETE here!
                // This is a seamless transition, not a stop event

                // Start playing next track immediately
                // This will fire READY_FOR_DISPLAY, but navigation handler will see
                // we're already on Now Playing and just fire SCREEN_READY
                requestPlay(nextTrack);
            });

            return;  // Exit early - we're handling it via queue
        }
    }

    // No queue or queue empty - use standard stop flow with NATURAL_COMPLETION reason
    Serial.println("[PlaybackCoordinator] No next track, stopping with NATURAL_COMPLETION");

    requestStop(StopReason::NATURAL_COMPLETION);

    // This will eventually fire EVENT_PLAYBACK_STOPPED_COMPLETE with NATURAL_COMPLETION
    // Navigation handler will see empty queue and navigate back
}

// ========================================
// Main Loop
// ========================================

void PlaybackCoordinator::update() {
    // Currently minimal - state machine is callback-driven

    // Future features:
    // - Timeout detection (if stuck in a state too long)
    // - Async operation monitoring
    // - Watchdog for hung operations
}

// ========================================
// Helper Methods
// ========================================

void PlaybackCoordinator::transitionTo(CoordinatorState newState) {
    if (state_ == newState) {
        return;  // No change
    }

    Serial.printf("[PlaybackCoordinator] State transition: %s → %s\n",
                 getStateName(state_), getStateName(newState));

    state_ = newState;
}

const char* PlaybackCoordinator::getStateName(CoordinatorState state) const {
    switch (state) {
        case CoordinatorState::IDLE:                return "IDLE";
        case CoordinatorState::LOADING:             return "LOADING";
        case CoordinatorState::READY_TO_DISPLAY:    return "READY_TO_DISPLAY";
        case CoordinatorState::STARTING_PLAYBACK:   return "STARTING_PLAYBACK";
        case CoordinatorState::PLAYING:             return "PLAYING";
        case CoordinatorState::STOPPING:            return "STOPPING";
        case CoordinatorState::STOPPED:             return "STOPPED";
        default:                                     return "UNKNOWN";
    }
}

void PlaybackCoordinator::onScreenReadyCallback(void* userData) {
    // Cast userData to PlaybackCoordinator instance and call instance method
    PlaybackCoordinator* instance = static_cast<PlaybackCoordinator*>(userData);
    if (instance) {
        instance->onScreenReady();
    }
}
