#ifndef MODAL_DIALOG_H
#define MODAL_DIALOG_H

#include "../../retro_ui.h"
#include <Adafruit_RGBLCDShield.h>

/**
 * ModalDialog - DOS-style modal dialog boxes for confirmations, alerts, errors
 *
 * Features:
 * - Center-screen overlay with classic DOS double-line border
 * - Shadow effect for depth
 * - Button-driven (LEFT/RIGHT to select, SELECT to confirm)
 * - Auto-dismiss on selection
 * - Blocks underlying screen updates
 * - Classic DOS color scheme (light gray background, black text)
 *
 * Example:
 *   auto result = ModalDialog::showYesNo(ui, lcd, "Save File?", "Save changes?");
 *   if (result == ModalDialog::RESULT_YES) {
 *       // Save file
 *   }
 */
class ModalDialog {
public:
    enum Result {
        RESULT_NONE = -1,
        RESULT_YES = 0,
        RESULT_NO = 1,
        RESULT_CANCEL = 2,
        RESULT_OK = 3
    };

    enum Type {
        TYPE_YES_NO,          // "Yes" / "No"
        TYPE_YES_NO_CANCEL,   // "Yes" / "No" / "Cancel"
        TYPE_OK,              // "OK" (informational)
        TYPE_OK_CANCEL        // "OK" / "Cancel"
    };

    /**
     * Show a modal dialog and block until user responds
     * @param ui - RetroUI instance
     * @param lcd - LCD shield for button input
     * @param title - Dialog title (e.g., "Confirm Action")
     * @param message - Dialog message (up to 4 lines, auto-wrapped at 50 chars)
     * @param type - Dialog button configuration
     * @return Selected result
     */
    static Result show(RetroUI* ui,
                      Adafruit_RGBLCDShield* lcd,
                      const char* title,
                      const char* message,
                      Type type = TYPE_OK);

    // Convenience methods
    static Result showYesNo(RetroUI* ui, Adafruit_RGBLCDShield* lcd,
                            const char* title, const char* message) {
        return show(ui, lcd, title, message, TYPE_YES_NO);
    }

    static void showMessage(RetroUI* ui, Adafruit_RGBLCDShield* lcd,
                           const char* title, const char* message) {
        show(ui, lcd, title, message, TYPE_OK);
    }

    static Result showError(RetroUI* ui, Adafruit_RGBLCDShield* lcd,
                           const char* message) {
        return show(ui, lcd, "ERROR", message, TYPE_OK);
    }

    static Result showConfirm(RetroUI* ui, Adafruit_RGBLCDShield* lcd,
                             const char* message) {
        return show(ui, lcd, "Confirm", message, TYPE_OK_CANCEL);
    }

private:
    // Dialog layout constants (DOS style)
    static const uint8_t DIALOG_WIDTH = 60;    // Characters wide
    static const uint8_t DIALOG_MAX_HEIGHT = 14;  // Max characters tall
    static const uint8_t MESSAGE_WIDTH = 56;   // Message area width (with 2 char padding)
    static const uint8_t MAX_MESSAGE_LINES = 8; // Max lines for message text

    // Calculate dialog position (centered)
    static void getDialogPosition(uint8_t height, uint8_t* outCol, uint8_t* outRow);

    // Draw the dialog box
    static void drawDialog(RetroUI* ui, const char* title,
                          char messageLines[][MESSAGE_WIDTH + 1], uint8_t numMessageLines,
                          Type type, int selectedButton);

    // Draw a button (centered in button area)
    static void drawButton(RetroUI* ui, int col, int row,
                          const char* label, bool selected);

    // Draw shadow effect (DOS style - dark gray offset)
    static void drawShadow(RetroUI* ui, uint8_t col, uint8_t row,
                          uint8_t width, uint8_t height);

    // Get button count for dialog type
    static int getButtonCount(Type type);

    // Get button label for type and index
    static const char* getButtonLabel(Type type, int index);

    // Convert button index to result
    static Result buttonIndexToResult(Type type, int index);

    // Word-wrap message into lines (max MESSAGE_WIDTH chars per line)
    static uint8_t wrapMessage(const char* message, char messageLines[][MESSAGE_WIDTH + 1]);

    // Button debouncing
    static const uint32_t BUTTON_DEBOUNCE_MS = 150;
};

#endif // MODAL_DIALOG_H
