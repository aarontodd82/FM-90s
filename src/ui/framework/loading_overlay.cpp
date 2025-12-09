#include "loading_overlay.h"
#include "../../dos_colors.h"

LoadingOverlay::LoadingOverlay(RetroUI* ui)
    : ui_(ui),
      visible_(false),
      dimBackground_(false),  // Default to no dimming for better performance
      spinner_(nullptr) {
    message_[0] = '\0';
}

void LoadingOverlay::show(const char* message, bool dimBackground) {
    if (!message) return;

    strncpy(message_, message, sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
    dimBackground_ = dimBackground;
    visible_ = true;

    // Create spinner if not already created
    if (!spinner_) {
        uint8_t overlayCol, overlayRow;
        getOverlayPosition(&overlayCol, &overlayRow);

        // Spinner at bottom of overlay, centered
        uint8_t spinnerCol = overlayCol + (OVERLAY_WIDTH / 2) - 2;  // Center [|]
        uint8_t spinnerRow = overlayRow + OVERLAY_HEIGHT - 3;

        spinner_ = new ProgressIndicator(ui_, spinnerCol, spinnerRow, 4);
        spinner_->setStyle(ProgressIndicator::STYLE_SPINNER);
        spinner_->setColor(DOS_BRIGHT_CYAN);
    }

    drawOverlay();
}

void LoadingOverlay::update() {
    if (!visible_ || !spinner_) return;

    // Update spinner animation
    spinner_->update();
}

void LoadingOverlay::hide() {
    visible_ = false;

    // Clean up spinner
    if (spinner_) {
        delete spinner_;
        spinner_ = nullptr;
    }

    // Note: We do NOT redraw the screen here - caller must handle that
    // This is intentional to avoid flicker and give caller control
}

void LoadingOverlay::getOverlayPosition(uint8_t* outCol, uint8_t* outRow) {
    *outCol = (100 - OVERLAY_WIDTH) / 2;   // Center horizontally (100 = GRID_COLS)
    *outRow = (30 - OVERLAY_HEIGHT) / 2;   // Center vertically (30 = GRID_ROWS)
}

void LoadingOverlay::drawOverlay() {
    if (!visible_) return;

    uint8_t overlayCol, overlayRow;
    getOverlayPosition(&overlayCol, &overlayRow);

    // Optionally dim background
    if (dimBackground_) {
        // Draw a darker blue over the entire screen (simulates dimming)
        // We use a darker shade of blue to indicate modal state
        ui_->fillGridRect(0, 0, 100, 30, DOS_BLUE >> 1);  // Halve RGB values
    }

    // Draw shadow (offset 1 col, 1 row)
    ui_->fillGridRect(overlayCol + 1, overlayRow + 1, OVERLAY_WIDTH, OVERLAY_HEIGHT, DOS_DARK_GRAY);

    // Draw main overlay box with double-line border (DOS style)
    ui_->drawWindow(overlayCol, overlayRow, OVERLAY_WIDTH, OVERLAY_HEIGHT,
                   nullptr, DOS_WHITE, DOS_LIGHT_GRAY);

    // Word-wrap and draw message
    char lines[3][48];
    uint8_t numLines = wrapMessage(message_, lines);

    // Center message vertically in overlay
    uint8_t messageStartRow = overlayRow + 2 + ((OVERLAY_HEIGHT - 5 - numLines) / 2);

    for (uint8_t i = 0; i < numLines; i++) {
        // Center each line horizontally
        uint8_t lineLen = strlen(lines[i]);
        uint8_t lineCol = overlayCol + (OVERLAY_WIDTH - lineLen) / 2;

        ui_->drawText(lineCol, messageStartRow + i, lines[i],
                     DOS_BLACK, DOS_LIGHT_GRAY);
    }

    // Draw spinner
    if (spinner_) {
        spinner_->draw();
    }
}

uint8_t LoadingOverlay::wrapMessage(const char* message, char lines[][48]) {
    if (!message) return 0;

    uint8_t lineCount = 0;
    uint8_t messageLen = strlen(message);
    uint8_t pos = 0;
    const uint8_t lineWidth = 46;  // 48 - 2 for padding

    while (pos < messageLen && lineCount < 3) {
        uint8_t lineEnd = pos;
        uint8_t lastSpace = pos;
        bool foundNewline = false;

        // Find line break point
        while ((lineEnd - pos) < lineWidth && lineEnd < messageLen) {
            if (message[lineEnd] == '\n') {
                foundNewline = true;
                break;
            }
            if (message[lineEnd] == ' ') {
                lastSpace = lineEnd;
            }
            lineEnd++;
        }

        // Break at space if we hit width limit
        if (!foundNewline && (lineEnd - pos) >= lineWidth && lastSpace > pos) {
            lineEnd = lastSpace;
        }

        // Copy line
        uint8_t lineLen = lineEnd - pos;
        if (lineLen > 47) lineLen = 47;

        strncpy(lines[lineCount], message + pos, lineLen);
        lines[lineCount][lineLen] = '\0';

        // Trim leading/trailing spaces
        char* line = lines[lineCount];
        while (*line == ' ') line++;
        if (line != lines[lineCount]) {
            memmove(lines[lineCount], line, strlen(line) + 1);
        }

        lineCount++;

        // Move to next line
        if (foundNewline) {
            pos = lineEnd + 1;
        } else {
            pos = lineEnd;
            while (pos < messageLen && message[pos] == ' ') {
                pos++;
            }
        }
    }

    return lineCount;
}
