#ifndef INFO_PANEL_H
#define INFO_PANEL_H

#include "../../retro_ui.h"

/**
 * InfoPanel - DOS-style dedicated area for status, reminders, multi-line info
 *
 * Use cases:
 * - Bluetooth: "REMINDER: Put device in pairing mode"
 * - File browser: "3 files selected"
 * - Settings: "Changes not saved"
 * - Status messages: "Connected to: Device XYZ"
 *
 * Features:
 * - Single-line or multi-line text display
 * - Optional border (DOS-style panel)
 * - Color-coded for different message types (info, warning, error, reminder)
 * - Automatic word-wrapping
 * - Persistent display (not auto-dismissing like status notifications)
 *
 * Example:
 *   InfoPanel panel(ui, 5, 10, 60, 4);
 *   panel.showReminder("Put Bluetooth device in pairing mode");
 *   panel.draw();
 */
class InfoPanel {
public:
    /**
     * Create an info panel
     * @param ui - RetroUI instance
     * @param col - Column position
     * @param row - Row position
     * @param width - Width in characters
     * @param height - Height in lines
     */
    InfoPanel(RetroUI* ui, int col, int row, int width, int height);

    /**
     * Set text content (single string, auto-wrapped)
     * @param text - Text to display
     */
    void setText(const char* text);

    /**
     * Set content as individual lines (no wrapping)
     * @param line1 - First line (required)
     * @param line2 - Second line (optional)
     * @param line3 - Third line (optional)
     */
    void setLines(const char* line1, const char* line2 = nullptr,
                  const char* line3 = nullptr);

    /**
     * Set custom colors
     * @param fg - Foreground color
     * @param bg - Background color
     */
    void setColors(uint16_t fg, uint16_t bg);

    /**
     * Enable/disable border
     * @param enabled - If true, draws panel border
     */
    void setBorder(bool enabled);

    /**
     * Draw the panel
     */
    void draw();

    /**
     * Clear the panel
     */
    void clear();

    // Convenience methods for common panel types
    void showReminder(const char* text);   // Yellow background
    void showStatus(const char* text);     // Blue background
    void showWarning(const char* text);    // Brown background
    void showError(const char* text);      // Red background

private:
    RetroUI* ui_;
    int col_;
    int row_;
    int width_;
    int height_;
    char lines_[8][128];  // Support up to 8 lines
    int lineCount_;
    uint16_t fgColor_;
    uint16_t bgColor_;
    bool border_;

    // Word-wrap text into lines
    void wrapText(const char* text);
};

#endif // INFO_PANEL_H
