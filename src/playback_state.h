#ifndef PLAYBACK_STATE_H
#define PLAYBACK_STATE_H

#include <Arduino.h>
#include "audio_player_interface.h"  // FileFormat now defined here

/**
 * Global Playback State Manager
 *
 * Tracks what's currently playing across all screens.
 * Allows any screen to check playback status and display "Now Playing" info.
 */

enum PlaybackStatus {
    PLAYBACK_STOPPED,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED
};

class PlaybackState {
private:
    static PlaybackState* instance;

    // Current playback info
    PlaybackStatus status;
    String currentFile;
    String currentPath;  // Full path: "/Music/Genesis/greenhil.fm9"
    FileFormat format;

    // Timing
    uint32_t startTime;
    uint32_t pauseTime;
    uint32_t totalDuration; // milliseconds (0 if unknown)
    uint32_t currentPosition; // milliseconds (updated by player)
    bool isLooping; // True for formats that loop (VGM, FM90s with loop flag, RAD)
    bool hasPlayerPosition; // True if player is providing position updates

    // Voice stats (updated by synth)
    uint8_t voices2Op;
    uint8_t voices4Op;
    uint8_t voicesDrum;

    PlaybackState() : status(PLAYBACK_STOPPED), format(FileFormat::UNKNOWN),
                      startTime(0), pauseTime(0), totalDuration(0), currentPosition(0),
                      isLooping(false), hasPlayerPosition(false),
                      voices2Op(0), voices4Op(0), voicesDrum(0) {}

public:
    static PlaybackState* getInstance() {
        if (!instance) {
            instance = new PlaybackState();
        }
        return instance;
    }

    // Control playback state
    void startPlayback(const String& filePath, FileFormat fmt, bool looping = false) {
        currentPath = filePath;

        // Extract filename from path
        int lastSlash = filePath.lastIndexOf('/');
        currentFile = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;

        format = fmt;
        status = PLAYBACK_PLAYING;
        startTime = millis();
        pauseTime = 0;
        totalDuration = 0; // Unknown until player provides it
        currentPosition = 0;
        isLooping = looping;

        // Serial.printf("[PlaybackState] Started: %s (format: %d, looping: %d)\n",
        //              currentFile.c_str(), fmt, looping);
    }

    void stopPlayback() {
        status = PLAYBACK_STOPPED;
        currentFile = "";
        currentPath = "";
        startTime = 0;
        pauseTime = 0;
        totalDuration = 0;
        currentPosition = 0;
        isLooping = false;
        hasPlayerPosition = false;
        voices2Op = 0;
        voices4Op = 0;
        voicesDrum = 0;

        // // Serial.println("[PlaybackState] Stopped");
    }

    void pausePlayback() {
        if (status == PLAYBACK_PLAYING) {
            status = PLAYBACK_PAUSED;
            pauseTime = millis();
        }
    }

    void resumePlayback() {
        if (status == PLAYBACK_PAUSED) {
            status = PLAYBACK_PLAYING;
            // Adjust start time to account for pause
            startTime += (millis() - pauseTime);
            pauseTime = 0;
        }
    }

    // Getters
    bool isPlaying() const { return status == PLAYBACK_PLAYING; }
    bool isStopped() const { return status == PLAYBACK_STOPPED; }
    bool isPaused() const { return status == PLAYBACK_PAUSED; }

    PlaybackStatus getStatus() const { return status; }
    String getCurrentFile() const { return currentFile; }
    String getCurrentPath() const { return currentPath; }
    FileFormat getFormat() const { return format; }

    // Get elapsed time in milliseconds
    uint32_t getElapsedTime() const {
        if (status == PLAYBACK_STOPPED) return 0;
        // Use player-reported position if player is providing updates
        if (hasPlayerPosition) return currentPosition;
        // Otherwise fall back to millis()
        if (status == PLAYBACK_PAUSED) return pauseTime - startTime;
        return millis() - startTime;
    }

    // Duration and position management (called by main loop to update from player)
    void setDuration(uint32_t durationMs) { totalDuration = durationMs; }
    uint32_t getDuration() const { return totalDuration; }

    void setPosition(uint32_t positionMs) {
        currentPosition = positionMs;
        hasPlayerPosition = true;  // Mark that player is providing position
    }
    uint32_t getPosition() const { return currentPosition; }

    void setLooping(bool looping) { isLooping = looping; }
    bool getIsLooping() const { return isLooping; }

    // Voice stats
    void updateVoiceStats(uint8_t v2op, uint8_t v4op, uint8_t vdrum) {
        voices2Op = v2op;
        voices4Op = v4op;
        voicesDrum = vdrum;
    }

    uint8_t get2OpVoices() const { return voices2Op; }
    uint8_t get4OpVoices() const { return voices4Op; }
    uint8_t getDrumVoices() const { return voicesDrum; }

    // Format the elapsed time as MM:SS
    String getElapsedTimeString() const {
        uint32_t elapsed = getElapsedTime();
        int seconds = elapsed / 1000;
        int minutes = seconds / 60;
        seconds = seconds % 60;

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
        return String(buf);
    }

    // Format total duration as MM:SS
    String getDurationString() const {
        if (totalDuration == 0) return "--:--";

        int seconds = totalDuration / 1000;
        int minutes = seconds / 60;
        seconds = seconds % 60;

        char buf[20];
        if (isLooping) {
            // Show duration with LOOP indicator
            snprintf(buf, sizeof(buf), "%02d:%02d LOOP", minutes, seconds);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
        }
        return String(buf);
    }

    // Get progress as 0.0-1.0 (for progress bars)
    // For looping tracks, wraps back to 0.0 when exceeding 1.0
    float getProgress() const {
        if (totalDuration == 0) return 0.0f;
        uint32_t elapsed = getElapsedTime();
        float progress = (float)elapsed / (float)totalDuration;

        // For looping tracks, wrap progress (modulo 1.0)
        if (isLooping && progress > 1.0f) {
            progress = fmodf(progress, 1.0f);
        }

        return min(1.0f, progress);
    }

    // Get format name for display
    const char* getFormatName() const {
        return fileFormatToString(format);
    }
};

// Global instance
extern PlaybackState* g_playbackState;

#endif // PLAYBACK_STATE_H
