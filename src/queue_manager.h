#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "ui/framework/event_manager.h"

/**
 * QueueManager - Simple playback queue for sequential song playback
 *
 * Part of Queue System (Phase 1-5 implementation)
 * See docs/QUEUE_SYSTEM_DESIGN.md for full architecture details
 *
 * Features:
 * - Queue tracks from SD card (most reliable source)
 * - Auto-advance on natural completion (via PlaybackCoordinator)
 * - Manual next/previous navigation with history tracking
 * - Event-driven updates for UI synchronization
 * - Persistent queue (survives stop operation)
 *
 * Architecture:
 * - queue_[0] = current/next track to play (slot 0)
 * - queue_[1+] = upcoming tracks
 * - history_ = recently played tracks (for "previous" navigation)
 *
 * Integration Points:
 * - FileBrowserScreenNew: Add files/folders to queue (SD only)
 * - PlaybackCoordinator: Query hasNext(), advance on natural completion
 * - NowPlayingScreen: Display "Up Next", Next/Previous actions
 * - MainMenuScreen: Show dynamic "Current Queue" menu item
 *
 * Memory Usage: ~2KB for 20 tracks + history (64 bytes per path average)
 */
class QueueManager {
public:
    /**
     * Constructor
     */
    QueueManager();

    // ============================================
    // QUEUE OPERATIONS
    // ============================================

    /**
     * Add a track to the end of the queue
     * @param filePath - Full path to file (e.g., "/Music/song.mid")
     */
    void addToQueue(const char* filePath);

    /**
     * Insert a track at position 1 (play after current track)
     * @param filePath - Full path to file
     */
    void insertNext(const char* filePath);

    /**
     * Clear entire queue (including current track)
     */
    void clear();

    /**
     * Remove a specific track by index
     * @param index - Queue index (0 = current track)
     * @return true if removed successfully
     */
    bool removeAt(int index);


    // ============================================
    // NAVIGATION
    // ============================================

    /**
     * Advance to next track in queue
     * - Removes slot 0, returns it
     * @param currentTrack - Unused (kept for compatibility)
     * @return Next track path, or nullptr if no next track
     */
    const char* playNext(const char* currentTrack);

    /**
     * Get current track (slot 0)
     * @return File path or nullptr if queue empty
     */
    const char* getCurrentTrack();

    /**
     * Get next track (slot 1)
     * @return File path or nullptr if no next track
     */
    const char* getNextTrack();

    /**
     * Get any track by index
     * @param index - Queue index (0 = current, 1 = next, etc.)
     * @return File path or nullptr if index out of range
     */
    const char* getTrackAt(int index);

    // ============================================
    // QUEUE INFO
    // ============================================

    /**
     * Get total number of tracks in queue
     * @return Queue size (including current track)
     */
    int getQueueSize() const;

    /**
     * Get current playing index (always 0 for simple queue)
     * @return 0 if queue has tracks, -1 if empty
     */
    int getCurrentIndex() const;

    /**
     * Check if queue is empty
     * @return true if no tracks in queue
     */
    bool isEmpty() const;

    /**
     * Check if there's a next track
     * @return true if queue is not empty
     */
    bool hasNext() const;

    // ============================================
    // INTEGRATION
    // ============================================

    /**
     * Set event manager for firing queue change events
     * @param em - EventManager instance
     */
    void setEventManager(EventManager* em);

private:
    std::vector<String> queue_;     // File paths (slot 0 = next track to play)
    EventManager* eventManager_;    // For firing events

    /**
     * Fire an event if EventManager is set
     * @param type - Event type to fire
     */
    void fireEvent(EventManager::EventType type);

    /**
     * Fire an event with integer parameter
     * @param type - Event type to fire
     * @param value - Integer value
     */
    void fireEventInt(EventManager::EventType type, int value);
};

#endif // QUEUE_MANAGER_H
