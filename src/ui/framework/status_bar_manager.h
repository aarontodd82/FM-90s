#ifndef STATUS_BAR_MANAGER_H
#define STATUS_BAR_MANAGER_H

#include "../../retro_ui.h"
#include "event_manager.h"
#include "../../playback_state.h"
#include "../../queue_manager.h"
#include "../screen_id.h"
#include "../../dos_colors.h"
#include <Arduino.h>

/**
 * StatusBarManager - Dynamic global status bar system
 *
 * Purpose: Show "Now:" and "Next:" across all screens with
 * automatic event-driven updates.
 *
 * Features:
 * - Shows "Now: [song]" when something is playing (everywhere except Now Playing screen)
 * - Shows "Next: [song]" when queue has tracks (everywhere including Now Playing screen)
 * - Event-driven updates (no screen redraws needed)
 * - Respects notification space on the right side
 * - Incremental updates only (draws only changed parts)
 * - Context-aware (knows current screen to hide redundant info)
 * - Shorter captions save precious screen space
 *
 * Integration:
 * - Add to ScreenContext
 * - Call setCurrentScreen() when screens change
 * - Screens call draw() in drawFooter() and update() in their update()
 * - StatusBarManager handles all event subscriptions automatically
 *
 * Example Usage:
 *   // In screen's drawFooter():
 *   context_->statusBarManager->draw();
 *
 *   // In screen's update():
 *   context_->statusBarManager->update();
 *
 *   // In ScreenManager when switching screens:
 *   statusBarManager->setCurrentScreen(newScreenID);
 */
class StatusBarManager {
public:
    StatusBarManager(RetroUI* ui, EventManager* eventManager,
                     PlaybackState* playbackState, QueueManager* queueManager);
    ~StatusBarManager();

    /**
     * Initialize event listeners
     * Call once during system setup
     */
    void begin();

    /**
     * Set current screen (called by ScreenManager)
     * Used to hide redundant info (e.g., "Now:" on Now Playing screen)
     */
    void setCurrentScreen(ScreenID screenID);

    /**
     * Draw the status bar (call from screen's drawFooter())
     * This does a full draw of the status bar
     */
    void draw();

    /**
     * Update status bar if needed (call from screen's update())
     * This only redraws if content has changed
     * Returns true if redraw occurred
     */
    bool update();

    /**
     * Force a full redraw on next update()
     * Useful after screen transitions
     */
    void requestRedraw();

private:
    // Dependencies
    RetroUI* ui_;
    EventManager* eventManager_;
    PlaybackState* playbackState_;
    QueueManager* queueManager_;

    // Current state
    ScreenID currentScreen_;
    String lastDrawnText_;  // Dirty checking - only redraw if changed
    bool needsRedraw_;      // Force redraw flag

    // Notification space tracking (from RetroUI)
    static const int MAX_NOTIFICATION_LENGTH = 50;  // Reserve space on right

    /**
     * Build the status bar text based on current playback/queue state
     * @return Status bar text (left-aligned, respects notification space)
     */
    String buildStatusText();

    /**
     * Truncate text to fit available space (accounting for notifications)
     * @param text - Text to truncate
     * @param maxLength - Maximum length
     * @return Truncated text with "..." if needed
     */
    String truncateText(const String& text, int maxLength);

    /**
     * Extract filename from full path
     * @param path - Full file path
     * @return Filename only
     */
    String getFilenameFromPath(const String& path);

    // Event callbacks
    static void onPlaybackStarted(void* userData);
    static void onPlaybackStopped(int stopReason, void* userData);
    static void onQueueChanged(void* userData);
};

#endif // STATUS_BAR_MANAGER_H
