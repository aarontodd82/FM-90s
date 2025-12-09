#include "modal_dialog.h"
#include "../../dos_colors.h"

// Show modal dialog and wait for user input
ModalDialog::Result ModalDialog::show(RetroUI* ui,
                                      Adafruit_RGBLCDShield* lcd,
                                      const char* title,
                                      const char* message,
                                      Type type) {
    if (!ui || !lcd || !title || !message) {
        return RESULT_NONE;
    }

    // Word-wrap message into lines
    char messageLines[MAX_MESSAGE_LINES][MESSAGE_WIDTH + 1];
    uint8_t numMessageLines = wrapMessage(message, messageLines);

    // Calculate dialog height (title bar + message + button area + borders)
    // Layout: [border][title][border][message lines][spacing][buttons][border]
    uint8_t dialogHeight = 3 + numMessageLines + 1 + 3 + 1;  // 3=title area, 1=spacing, 3=button area, 1=bottom border
    if (dialogHeight > DIALOG_MAX_HEIGHT) {
        dialogHeight = DIALOG_MAX_HEIGHT;
        numMessageLines = DIALOG_MAX_HEIGHT - 8;  // Truncate message if too long
    }

    // Calculate centered position
    uint8_t dialogCol, dialogRow;
    getDialogPosition(dialogHeight, &dialogCol, &dialogRow);

    // Save the screen region where dialog will appear (DOS-style save/restore)
    // Include shadow area (extends 1 col right and 1 row down)
    RetroUI::SavedRegion* savedRegion = ui->saveRegion(dialogCol, dialogRow,
                                                        DIALOG_WIDTH + 1, dialogHeight + 1);

    // Button state
    int buttonCount = getButtonCount(type);
    int selectedButton = 0;  // Default to first button
    uint32_t lastButtonTime = 0;

    // Modal loop - block until user makes selection
    bool done = false;
    Result result = RESULT_NONE;
    bool needsRedraw = true;  // Draw once initially

    while (!done) {
        // Only redraw when selection changes or initially
        if (needsRedraw) {
            drawDialog(ui, title, messageLines, numMessageLines, type, selectedButton);
            needsRedraw = false;
        }

        // Wait for button press
        delay(10);  // Small delay to prevent CPU spinning
        uint8_t buttons = lcd->readButtons();
        uint32_t now = millis();

        // Debounce buttons
        if (now - lastButtonTime < BUTTON_DEBOUNCE_MS) {
            continue;
        }

        // Handle button navigation (LEFT/RIGHT since buttons are horizontal)
        if (buttons & BUTTON_LEFT) {
            selectedButton--;
            if (selectedButton < 0) {
                selectedButton = buttonCount - 1;  // Wrap to right
            }
            lastButtonTime = now;
            needsRedraw = true;  // Redraw with new selection
        } else if (buttons & BUTTON_RIGHT) {
            selectedButton++;
            if (selectedButton >= buttonCount) {
                selectedButton = 0;  // Wrap to left
            }
            lastButtonTime = now;
            needsRedraw = true;  // Redraw with new selection
        } else if (buttons & BUTTON_SELECT) {
            // User confirmed selection
            result = buttonIndexToResult(type, selectedButton);
            done = true;
            lastButtonTime = now;
        }
    }

    // Wait for button release before returning (prevent double-trigger)
    while (lcd->readButtons() != 0) {
        delay(10);
    }

    // Restore the saved screen region (removes modal without full screen redraw)
    if (savedRegion && savedRegion->isValid()) {
        ui->restoreRegion(savedRegion);
    }
    delete savedRegion;

    return result;
}

// Calculate centered dialog position
void ModalDialog::getDialogPosition(uint8_t height, uint8_t* outCol, uint8_t* outRow) {
    *outCol = (100 - DIALOG_WIDTH) / 2;  // Center horizontally (100 = GRID_COLS)
    *outRow = (30 - height) / 2;         // Center vertically (30 = GRID_ROWS)
}

// Draw the dialog box with DOS styling
void ModalDialog::drawDialog(RetroUI* ui, const char* title,
                             char messageLines[][MESSAGE_WIDTH + 1], uint8_t numMessageLines,
                             Type type, int selectedButton) {
    uint8_t dialogHeight = 3 + numMessageLines + 1 + 3 + 1;
    uint8_t dialogCol, dialogRow;
    getDialogPosition(dialogHeight, &dialogCol, &dialogRow);

    // Draw shadow first (offset by 1 col, 1 row)
    drawShadow(ui, dialogCol + 1, dialogRow + 1, DIALOG_WIDTH, dialogHeight);

    // Draw main dialog window with double-line border
    ui->drawWindow(dialogCol, dialogRow, DIALOG_WIDTH, dialogHeight,
                  title, DOS_BLACK, DOS_LIGHT_GRAY);

    // Draw message lines (starting at row 3, after title bar)
    uint8_t messageRow = dialogRow + 3;
    for (uint8_t i = 0; i < numMessageLines; i++) {
        ui->drawText(dialogCol + 2, messageRow + i, messageLines[i],
                    DOS_BLACK, DOS_LIGHT_GRAY);
    }

    // Draw buttons (at bottom of dialog)
    int buttonCount = getButtonCount(type);
    uint8_t buttonRow = dialogRow + dialogHeight - 3;  // 3 rows from bottom

    // Calculate button layout (centered)
    // Each button is approximately 12 chars wide (with spacing)
    int totalButtonWidth = buttonCount * 12;
    uint8_t buttonStartCol = dialogCol + (DIALOG_WIDTH - totalButtonWidth) / 2;

    for (int i = 0; i < buttonCount; i++) {
        const char* label = getButtonLabel(type, i);
        bool isSelected = (i == selectedButton);

        // Button position (space them 12 chars apart)
        uint8_t btnCol = buttonStartCol + (i * 12);

        drawButton(ui, btnCol, buttonRow, label, isSelected);
    }
}

// Draw a DOS-style button
void ModalDialog::drawButton(RetroUI* ui, int col, int row,
                             const char* label, bool selected) {
    // Button is 10 chars wide
    uint8_t buttonWidth = 10;

    // Button colors (DOS style)
    uint16_t fg, bg;
    if (selected) {
        fg = DOS_YELLOW;           // Yellow text for selected
        bg = DOS_BLACK;            // Black background for selected
    } else {
        fg = DOS_BLACK;            // Black text for normal
        bg = DOS_LIGHT_GRAY;       // Gray background for normal
    }

    // Draw button background
    ui->fillGridRect(col, row, buttonWidth, 1, bg);

    // Draw button border (simple brackets for DOS look)
    if (selected) {
        ui->drawText(col, row, "[", fg, bg);
        ui->drawText(col + buttonWidth - 1, row, "]", fg, bg);
    } else {
        ui->drawText(col, row, " ", fg, bg);
        ui->drawText(col + buttonWidth - 1, row, " ", fg, bg);
    }

    // Center the label within the button
    uint8_t labelLen = strlen(label);
    uint8_t labelCol = col + (buttonWidth - labelLen) / 2;
    ui->drawText(labelCol, row, label, fg, bg);
}

// Draw shadow effect (DOS style)
void ModalDialog::drawShadow(RetroUI* ui, uint8_t col, uint8_t row,
                             uint8_t width, uint8_t height) {
    // Classic DOS shadow: dark gray, offset 1 col and 1 row to bottom-right
    // Shadow on right side
    ui->fillGridRect(col + width, row + 1, 1, height, DOS_DARK_GRAY);

    // Shadow on bottom
    ui->fillGridRect(col + 1, row + height, width, 1, DOS_DARK_GRAY);
}

// Get number of buttons for dialog type
int ModalDialog::getButtonCount(Type type) {
    switch (type) {
        case TYPE_YES_NO:
            return 2;
        case TYPE_YES_NO_CANCEL:
            return 3;
        case TYPE_OK:
            return 1;
        case TYPE_OK_CANCEL:
            return 2;
        default:
            return 1;
    }
}

// Get button label for type and index
const char* ModalDialog::getButtonLabel(Type type, int index) {
    switch (type) {
        case TYPE_YES_NO:
            return (index == 0) ? "Yes" : "No";

        case TYPE_YES_NO_CANCEL:
            if (index == 0) return "Yes";
            else if (index == 1) return "No";
            else return "Cancel";

        case TYPE_OK:
            return "OK";

        case TYPE_OK_CANCEL:
            return (index == 0) ? "OK" : "Cancel";

        default:
            return "OK";
    }
}

// Convert button index to result
ModalDialog::Result ModalDialog::buttonIndexToResult(Type type, int index) {
    switch (type) {
        case TYPE_YES_NO:
            return (index == 0) ? RESULT_YES : RESULT_NO;

        case TYPE_YES_NO_CANCEL:
            if (index == 0) return RESULT_YES;
            else if (index == 1) return RESULT_NO;
            else return RESULT_CANCEL;

        case TYPE_OK:
            return RESULT_OK;

        case TYPE_OK_CANCEL:
            return (index == 0) ? RESULT_OK : RESULT_CANCEL;

        default:
            return RESULT_NONE;
    }
}

// Word-wrap message into lines
uint8_t ModalDialog::wrapMessage(const char* message, char messageLines[][MESSAGE_WIDTH + 1]) {
    if (!message) return 0;

    uint8_t lineCount = 0;
    uint8_t messageLen = strlen(message);
    uint8_t pos = 0;

    while (pos < messageLen && lineCount < MAX_MESSAGE_LINES) {
        // Find the end of this line (either MESSAGE_WIDTH chars or newline)
        uint8_t lineEnd = pos;
        uint8_t lastSpace = pos;
        bool foundNewline = false;

        // Scan forward to find line break point
        while ((lineEnd - pos) < MESSAGE_WIDTH && lineEnd < messageLen) {
            if (message[lineEnd] == '\n') {
                foundNewline = true;
                break;
            }
            if (message[lineEnd] == ' ') {
                lastSpace = lineEnd;
            }
            lineEnd++;
        }

        // If we hit MESSAGE_WIDTH and there's a space, break at the space
        if (!foundNewline && (lineEnd - pos) >= MESSAGE_WIDTH && lastSpace > pos) {
            lineEnd = lastSpace;
        }

        // Copy line (skip newline character)
        uint8_t lineLen = lineEnd - pos;
        if (lineLen > MESSAGE_WIDTH) {
            lineLen = MESSAGE_WIDTH;
        }

        strncpy(messageLines[lineCount], message + pos, lineLen);
        messageLines[lineCount][lineLen] = '\0';

        // Trim leading/trailing spaces
        // Trim leading spaces
        char* line = messageLines[lineCount];
        while (*line == ' ') line++;
        if (line != messageLines[lineCount]) {
            memmove(messageLines[lineCount], line, strlen(line) + 1);
        }

        lineCount++;

        // Move to next line
        if (foundNewline) {
            pos = lineEnd + 1;  // Skip the newline
        } else {
            pos = lineEnd;
            // Skip spaces at start of next line
            while (pos < messageLen && message[pos] == ' ') {
                pos++;
            }
        }
    }

    return lineCount;
}
