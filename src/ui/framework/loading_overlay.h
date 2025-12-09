#ifndef LOADING_OVERLAY_H
#define LOADING_OVERLAY_H

#include "../../retro_ui.h"
#include "progress_indicator.h"

/**
 * LoadingOverlay - DOS-style overlay for async operations
 *
 * Features:
 * - Centered message box with spinner
 * - Optional dimming of background (darker blue)
 * - Shadow effect for depth
 * - Animated spinner
 * - Non-blocking (call update() to animate)
 *
 * Example:
 *   LoadingOverlay overlay(ui);
 *   overlay.show("Loading file, please wait...");
 *
 *   while (loading) {
 *       overlay.update();  // Animate spinner
 *       // ... do work ...
 *   }
 *
 *   overlay.hide();
 */
class LoadingOverlay {
public:
    /**
     * Create a loading overlay
     * @param ui - RetroUI instance
     */
    LoadingOverlay(RetroUI* ui);

    /**
     * Show the loading overlay
     * @param message - Message to display (up to 3 lines, auto-wrapped)
     * @param dimBackground - If true, dims background (default: false for performance)
     */
    void show(const char* message, bool dimBackground = false);

    /**
     * Update spinner animation (call periodically)
     */
    void update();

    /**
     * Hide the overlay
     * Note: Does NOT restore previous screen content - caller must redraw
     */
    void hide();

    /**
     * Check if overlay is visible
     */
    bool isVisible() const { return visible_; }

private:
    RetroUI* ui_;
    bool visible_;
    char message_[128];
    bool dimBackground_;

    // Overlay dimensions
    static const uint8_t OVERLAY_WIDTH = 50;
    static const uint8_t OVERLAY_HEIGHT = 8;

    // Spinner
    ProgressIndicator* spinner_;

    // Calculate centered position
    void getOverlayPosition(uint8_t* outCol, uint8_t* outRow);

    // Draw the overlay box
    void drawOverlay();

    // Word-wrap message into lines
    uint8_t wrapMessage(const char* message, char lines[][48]);
};

#endif // LOADING_OVERLAY_H
