#pragma once

#include <Arduino.h>
#include "audio_player_interface.h"
#include "player_config.h"
#include "playback_state.h"

// Forward declarations
class OPL3Synth;
class FileSource;
class DrumSamplerV2;
class AudioMixer4;
class AudioEffectFreeverb;
class EventManager;
class ScreenManager;

// ============================================
// PLAYBACK COORDINATOR CALLBACKS (Phase 1)
// ============================================
// These callbacks enable PlayerManager to report when operations
// are ACTUALLY complete (not guessed with delays).
// The PlaybackCoordinator will register callbacks to orchestrate
// the playback lifecycle.

#include <functional>

typedef std::function<void(bool success)> LoadCompleteCallback;
typedef std::function<void(bool success)> StartCompleteCallback;
typedef std::function<void()> StopCompleteCallback;
typedef std::function<void()> NaturalCompletionCallback;

/**
 * PlayerManager - Centralized player lifecycle management
 *
 * PURPOSE:
 * - On-demand player creation/destruction (only one active player at a time)
 * - Centralized play/stop operations (eliminates code duplication)
 * - Format-specific audio effect management (MIDI gets crossfeed/reverb)
 * - Safe player switching with automatic cleanup
 *
 * BENEFITS:
 * - Reduces RAM usage (only active player exists)
 * - Eliminates duplicated fade/mute logic across all players
 * - Single source of truth for player lifecycle
 * - Format-aware effect management
 *
 * USAGE:
 *   PlayerManager manager(config);
 *
 *   // Load and play a file (auto-detects format)
 *   if (manager.loadAndPlay("song.mid")) {
 *       // File loaded, player created, playing
 *   }
 *
 *   // Update in main loop
 *   manager.update();
 *
 *   // Stop and cleanup
 *   manager.stop();
 *
 * CENTRALIZED OPERATIONS:
 * - play():  setFadeGain(1.0) + format-specific effects
 * - stop():  setFadeGain(0.0) + disable effects + cleanup
 * - switch formats: auto cleanup old player before creating new
 */
class PlayerManager {
public:
    explicit PlayerManager(const PlayerConfig& config);
    ~PlayerManager();

    // ========================================
    // Primary API (Phase 2: Callback-Driven)
    // ========================================

    /**
     * Prepare file for playback (ASYNC)
     * - Auto-detects format from extension
     * - Creates appropriate player on-demand
     * - Handles player switching (stops old, creates new)
     * - Loads file into player
     * - ALL delays happen internally
     * - Calls callback when ACTUALLY complete
     *
     * @param path File path
     * @param callback Called with success status when load completes
     */
    void prepareFileAsync(const char* path, LoadCompleteCallback callback);

    /**
     * Start playback (ASYNC)
     * - Unmutes audio
     * - Enables format-specific effects
     * - Starts player
     * - Calls callback when ACTUALLY ready
     *
     * @param callback Called with success status when start completes
     */
    void startPlaybackAsync(StartCompleteCallback callback);

    /**
     * Stop playback (ASYNC)
     * - Stops player
     * - Mutes audio
     * - Disables effects
     * - ALL delays happen internally (ISR safety, hardware settle)
     * - Calls callback when ACTUALLY complete and safe
     *
     * @param callback Called when stop is fully complete
     */
    void stopAsync(StopCompleteCallback callback);

    /**
     * Register callback for natural playback completion
     * - Called by update() when song ends naturally
     * - Coordinator will handle stop sequence
     *
     * @param callback Called when song finishes naturally
     */
    void setNaturalCompletionCallback(NaturalCompletionCallback callback);

    /**
     * Update current player
     * - Call from main loop every iteration
     * - Handles player-specific update logic
     * - Synchronizes PlaybackState with player progress
     * - Detects natural completion and calls naturalCompletionCallback_
     */
    void update();

    /**
     * Pause/resume playback
     */
    void pause();
    void resume();

    // ========================================
    // State Queries
    // ========================================

    IAudioPlayer* getCurrentPlayer() const { return currentPlayer_; }
    FileFormat getCurrentFormat() const { return currentFormat_; }
    PlayerState getState() const;
    bool isPlaying() const;
    bool isPaused() const;

    // Playback info (forwards to current player)
    uint32_t getDurationMs() const;
    uint32_t getPositionMs() const;
    float getProgress() const;
    const char* getFileName() const;

    // ========================================
    // Optional Components (for GUI integration)
    // ========================================

    /**
     * Set EventManager (DEPRECATED - Phase 2)
     * - PlayerManager no longer fires events
     * - PlaybackCoordinator fires all events
     * - Kept for compatibility during refactoring
     */
    void setEventManager(EventManager* eventManager);

    /**
     * Set ScreenManager (deprecated - no longer used for auto-navigation)
     * - Auto-navigation is now handled by PlaybackEventHandler
     * - This method kept for compatibility but does nothing
     */
    void setScreenManager(ScreenManager* screenManager);

private:
    // ========================================
    // Player Creation (On-Demand)
    // ========================================

    IAudioPlayer* createPlayer(FileFormat format);
    void destroyCurrentPlayer();

    // Format detection
    FileFormat detectFormat(const char* path) const;

    // ========================================
    // Centralized Lifecycle Operations
    // ========================================

    /**
     * Centralized play logic - eliminates duplication across all players
     * - Unmutes audio (setFadeGain 1.0)
     * - Enables format-specific effects (MIDI: crossfeed, reverb)
     * - Calls player->play()
     */
    void centralizedPlay();

    /**
     * Centralized stop logic - eliminates duplication across all players
     * - Calls player->stop()
     * - Mutes audio (setFadeGain 0.0)
     * - Disables format-specific effects
     */
    void centralizedStop();

    /**
     * Format-specific effect management
     * - MIDI: Enable crossfeed + reverb (if user preferences allow)
     * - All others: Ensure effects are disabled
     */
    void applyFormatSpecificEffects(FileFormat format, bool enable);

    // ========================================
    // Member Variables
    // ========================================

    PlayerConfig config_;              // Dependency injection container
    IAudioPlayer* currentPlayer_;      // Currently active player (nullptr if none)
    FileFormat currentFormat_;         // Current player format

    // Cached for quick access
    OPL3Synth* opl3_;
    FileSource* fileSource_;
    DrumSamplerV2* drumSampler_;
    AudioMixer4* mixerLeft_;           // Main mixer (line-in on ch0, submixer on ch1)
    AudioMixer4* mixerRight_;          // Main mixer
    AudioMixer4* submixerLeft_;        // Submixer (ch0=dacNesMixer, ch1=SPC, ch2=GB, ch3=MOD)
    AudioMixer4* submixerRight_;       // Submixer
    AudioMixer4* dacNesMixerLeft_;     // DAC/NES pre-mixer (ch0=DAC, ch1=NES APU)
    AudioMixer4* dacNesMixerRight_;    // DAC/NES pre-mixer
    AudioMixer4* finalMixerLeft_;
    AudioMixer4* finalMixerRight_;
    AudioMixer4* fadeMixerLeft_;
    AudioMixer4* fadeMixerRight_;
    AudioEffectFreeverb* reverbLeft_;
    AudioEffectFreeverb* reverbRight_;

    // User preferences (from config)
    bool crossfeedEnabled_;
    bool reverbEnabled_;

    // Optional GUI integration
    PlaybackState* playbackState_;    // Singleton for UI state synchronization
    EventManager* eventManager_;      // For firing playback events (optional)

    // Phase 2: Callback-driven architecture
    LoadCompleteCallback loadCompleteCallback_;
    StartCompleteCallback startCompleteCallback_;
    StopCompleteCallback stopCompleteCallback_;
    NaturalCompletionCallback naturalCompletionCallback_;

    // Pending operation state
    String pendingFilePath_;          // File path being prepared
    FileFormat pendingFormat_;        // Format being prepared
};
