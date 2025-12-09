#ifndef AUDIO_PLAYER_INTERFACE_H
#define AUDIO_PLAYER_INTERFACE_H

#include <Arduino.h>
#include <functional>

/**
 * Unified Player State
 *
 * Replaces 6 different state enums with a single common enum.
 * All players will use this state representation.
 */
enum class PlayerState {
    IDLE,       // Player created but no file loaded
    LOADING,    // File is being loaded/parsed
    READY,      // File loaded, ready to play
    PLAYING,    // Currently playing
    PAUSED,     // Playback paused (can resume)
    STOPPING,   // In the process of stopping (transition state)
    STOPPED,    // Playback stopped (can replay)
    ERROR       // Error occurred during load or playback
};

/**
 * File Format Enum
 *
 * Moved from playback_state.h to here for better organization.
 * Represents the audio file format type.
 */
enum class FileFormat {
    UNKNOWN,
    MIDI,
    VGM,
    FM9,    // Extended VGM with embedded audio and FX automation
    SPC,
    MOD,    // Protracker MOD
    S3M,    // Scream Tracker 3
    XM,     // FastTracker II
    IT      // Impulse Tracker
};

/**
 * IAudioPlayer - Abstract Base Class
 *
 * All audio players (MIDI, VGM, SPC, MOD/S3M/XM/IT) must implement this interface.
 *
 * Design Goals:
 * - Unified API across all player types
 * - Type-safe polymorphism
 * - Consistent state management
 * - Predictable lifecycle
 *
 * Lifecycle Contract:
 *   IDLE → loadFile() → READY → play() → PLAYING → stop() → STOPPED
 *                                   ↓
 *                                 PAUSED ↔ pause()/resume()
 *
 * Thread Safety:
 * - All methods must be called from main loop, not ISR
 * - Implementations must handle timer ISR synchronization internally
 * - stop() must guarantee safe cleanup before returning
 */
class IAudioPlayer {
public:
    // Callback type for natural completion (song finished playing)
    using CompletionCallback = std::function<void()>;

    virtual ~IAudioPlayer() = default;

    // ============================================
    // LIFECYCLE MANAGEMENT
    // ============================================

    /**
     * Load an audio file
     *
     * @param path Absolute path to file (e.g., "/Music/song.mid")
     * @return true if file loaded successfully, false on error
     *
     * Post-conditions:
     * - On success: state = READY
     * - On failure: state = ERROR
     * - File handle opened and metadata parsed
     * - Duration calculated (if possible)
     */
    virtual bool loadFile(const char* path) = 0;

    /**
     * Start or resume playback
     *
     * Pre-conditions: state = READY or state = PAUSED
     * Post-conditions: state = PLAYING
     *
     * Responsibilities:
     * - Start internal timer (if applicable)
     * - Enable audio routing
     * - Unmute audio mixers
     * - Begin event processing
     */
    virtual void play() = 0;

    /**
     * Pause playback (can be resumed)
     *
     * Pre-conditions: state = PLAYING
     * Post-conditions: state = PAUSED
     *
     * Responsibilities:
     * - Stop internal timer
     * - Preserve playback position
     * - Keep audio connections active
     */
    virtual void pause() = 0;

    /**
     * Resume playback from pause
     *
     * Pre-conditions: state = PAUSED
     * Post-conditions: state = PLAYING
     *
     * Responsibilities:
     * - Resume internal timer
     * - Continue from saved position
     */
    virtual void resume() = 0;

    /**
     * Stop playback completely
     *
     * CRITICAL: This method must guarantee safe cleanup!
     *
     * Pre-conditions: Any state
     * Post-conditions: state = STOPPED
     *
     * Responsibilities (IN THIS ORDER):
     * 1. Stop internal timer/ISR
     * 2. delay(10) to ensure ISR completed
     * 3. Mute all audio mixers
     * 4. Disable audio effects (if any)
     * 5. Silence synthesizer/hardware
     * 6. Clear playback position
     * 7. Keep file loaded (allow replay)
     *
     * NOTE: Does NOT disconnect audio connections - AudioConnectionManager handles that!
     * NOTE: Does NOT delete the player object - PlayerManager handles that!
     */
    virtual void stop() = 0;

    /**
     * Update player state (called from main loop)
     *
     * Responsibilities:
     * - Process timer flags
     * - Update playback position
     * - Handle loop points
     * - Detect end of file
     * - Call completion callback when playback finishes naturally
     *
     * IMPORTANT: Must be called frequently (every loop iteration) for smooth playback
     */
    virtual void update() = 0;

    /**
     * Set callback for natural completion
     *
     * Called by player when playback finishes naturally (not via stop()).
     * Examples: song ends, fade completes, loop limit reached
     *
     * @param callback Function to call when playback completes, or nullptr to clear
     *
     * NOTE: Callback is called from update(), not from ISR
     * NOTE: Callback should NOT call stop() - player has already transitioned to STOPPED
     */
    virtual void setCompletionCallback(CompletionCallback callback) = 0;

    // ============================================
    // STATE QUERIES
    // ============================================

    /**
     * Get current player state
     * @return Current state enum value
     */
    virtual PlayerState getState() const = 0;

    /**
     * Check if currently playing
     * @return true if state == PLAYING
     */
    virtual bool isPlaying() const = 0;

    /**
     * Check if paused
     * @return true if state == PAUSED
     */
    virtual bool isPaused() const = 0;

    /**
     * Check if stopped
     * @return true if state == STOPPED
     */
    virtual bool isStopped() const = 0;

    // ============================================
    // PROGRESS TRACKING (UNIFIED API)
    // ============================================

    /**
     * Get total duration in milliseconds
     *
     * @return Duration in ms, or 0 if unknown/infinite loop
     *
     * NOTE: For looping formats (VGM), returns one loop iteration duration
     */
    virtual uint32_t getDurationMs() const = 0;

    /**
     * Get current playback position in milliseconds
     *
     * @return Position in ms from start of file
     *
     * NOTE: For looping formats, resets to 0 on loop
     */
    virtual uint32_t getPositionMs() const = 0;

    /**
     * Get playback progress as percentage
     *
     * @return Progress from 0.0 (start) to 1.0 (end)
     *
     * NOTE: For looping formats, wraps back to 0.0 after each loop
     */
    virtual float getProgress() const = 0;

    // ============================================
    // METADATA
    // ============================================

    /**
     * Get loaded filename
     * @return Filename without path, or empty string if no file loaded
     */
    virtual const char* getFileName() const = 0;

    /**
     * Get file format type
     * @return Format enum value (MIDI, VGM, etc.)
     */
    virtual FileFormat getFormat() const = 0;

    /**
     * Check if this format supports looping
     * @return true for VGM
     */
    virtual bool isLooping() const = 0;

    // ============================================
    // OPTIONAL: STATISTICS (for debugging)
    // ============================================

    /**
     * Print player statistics to Serial
     * (Optional - can be empty implementation)
     */
    virtual void printStats() const {}
};

/**
 * Helper function to convert PlayerState to string
 * Useful for debugging and logging
 */
inline const char* playerStateToString(PlayerState state) {
    switch (state) {
        case PlayerState::IDLE:     return "IDLE";
        case PlayerState::LOADING:  return "LOADING";
        case PlayerState::READY:    return "READY";
        case PlayerState::PLAYING:  return "PLAYING";
        case PlayerState::PAUSED:   return "PAUSED";
        case PlayerState::STOPPING: return "STOPPING";
        case PlayerState::STOPPED:  return "STOPPED";
        case PlayerState::ERROR:    return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/**
 * Helper function to convert FileFormat to string
 * Useful for debugging and logging
 */
inline const char* fileFormatToString(FileFormat format) {
    switch (format) {
        case FileFormat::UNKNOWN: return "UNKNOWN";
        case FileFormat::MIDI:    return "MIDI";
        case FileFormat::VGM:     return "VGM";
        case FileFormat::FM9:     return "FM9";
        case FileFormat::SPC:     return "SPC";
        case FileFormat::MOD:     return "MOD";
        case FileFormat::S3M:     return "S3M";
        case FileFormat::XM:      return "XM";
        case FileFormat::IT:      return "IT";
        default:                  return "INVALID";
    }
}

#endif // AUDIO_PLAYER_INTERFACE_H
