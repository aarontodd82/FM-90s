#ifndef PROGRESS_INDICATOR_H
#define PROGRESS_INDICATOR_H

#include "../../retro_ui.h"

/**
 * ProgressIndicator - DOS-style visual feedback for operations with known/unknown duration
 *
 * Styles:
 * - BAR: [##########----------] 50%
 * - BAR_TIME: [##########----------] 12s/20s
 * - PERCENTAGE: 50%
 * - SPINNER: [|] / [-] / [\] / [/] (animated, for indeterminate operations)
 *
 * Features:
 * - Classic DOS ASCII bar using # for filled, - for empty
 * - Automatic time formatting (seconds, minutes)
 * - Smooth spinner animation
 * - Optional label text
 * - Color-coded progress (cyan for normal, yellow for warning, red for critical)
 *
 * Example:
 *   ProgressIndicator progress(ui, 10, 15, 40);
 *   progress.setStyle(ProgressIndicator::STYLE_BAR_TIME);
 *   progress.setLabel("Loading");
 *   progress.setTime(elapsedMs, totalMs);
 *   progress.draw();
 */
class ProgressIndicator {
public:
    enum Style {
        STYLE_BAR,         // Progress bar with percentage
        STYLE_BAR_TIME,    // Progress bar with elapsed/total time
        STYLE_PERCENTAGE,  // Just percentage text
        STYLE_SPINNER      // Indeterminate spinner (rotating |/-\)
    };

    /**
     * Create a progress indicator
     * @param ui - RetroUI instance
     * @param col - Column position (grid coordinates)
     * @param row - Row position (grid coordinates)
     * @param width - Total width in characters (includes label if shown)
     */
    ProgressIndicator(RetroUI* ui, int col, int row, int width = 40);

    // Set progress (0.0 to 1.0)
    void setProgress(float progress);

    // Set time-based progress (for scans, loading, etc.)
    void setTime(unsigned long elapsedMs, unsigned long totalMs);

    // Set optional label (shown before progress bar)
    void setLabel(const char* label);

    // Set display style
    void setStyle(Style style);

    // Set progress bar color (default: cyan)
    void setColor(uint16_t color);

    // Display the progress indicator
    void draw();

    // Update animation (call periodically for spinner)
    void update();

    // Clear the progress indicator area
    void clear();

private:
    RetroUI* ui_;
    int col_;
    int row_;
    int width_;
    Style style_;
    float progress_;
    unsigned long elapsedMs_;
    unsigned long totalMs_;
    char label_[32];
    uint16_t barColor_;

    // Spinner animation state
    unsigned long lastSpinnerUpdate_;
    int spinnerFrame_;

    // Draw methods for each style
    void drawBar();
    void drawBarTime();
    void drawPercentage();
    void drawSpinner();

    // Helper to format time (e.g., "1m 23s", "45s")
    void formatTime(unsigned long ms, char* buffer, size_t bufferSize);
};

#endif // PROGRESS_INDICATOR_H
