#include "info_panel.h"
#include "../../dos_colors.h"

InfoPanel::InfoPanel(RetroUI* ui, int col, int row, int width, int height)
    : ui_(ui),
      col_(col),
      row_(row),
      width_(width),
      height_(height),
      lineCount_(0),
      fgColor_(DOS_WHITE),
      bgColor_(DOS_BLUE),
      border_(true) {

    // Initialize lines to empty
    for (int i = 0; i < 8; i++) {
        lines_[i][0] = '\0';
    }
}

void InfoPanel::setText(const char* text) {
    if (!text) {
        lineCount_ = 0;
        return;
    }

    wrapText(text);
}

void InfoPanel::setLines(const char* line1, const char* line2, const char* line3) {
    lineCount_ = 0;

    if (line1) {
        strncpy(lines_[lineCount_], line1, sizeof(lines_[0]) - 1);
        lines_[lineCount_][sizeof(lines_[0]) - 1] = '\0';
        lineCount_++;
    }

    if (line2) {
        strncpy(lines_[lineCount_], line2, sizeof(lines_[0]) - 1);
        lines_[lineCount_][sizeof(lines_[0]) - 1] = '\0';
        lineCount_++;
    }

    if (line3) {
        strncpy(lines_[lineCount_], line3, sizeof(lines_[0]) - 1);
        lines_[lineCount_][sizeof(lines_[0]) - 1] = '\0';
        lineCount_++;
    }
}

void InfoPanel::setColors(uint16_t fg, uint16_t bg) {
    fgColor_ = fg;
    bgColor_ = bg;
}

void InfoPanel::setBorder(bool enabled) {
    border_ = enabled;
}

void InfoPanel::draw() {
    if (border_) {
        // Draw panel with border
        ui_->drawPanel(col_, row_, width_, height_, nullptr, fgColor_, bgColor_);

        // Draw lines inside panel (with 1 char padding)
        int maxLines = height_ - 2;  // Leave room for top/bottom border
        int linesToDraw = (lineCount_ < maxLines) ? lineCount_ : maxLines;

        for (int i = 0; i < linesToDraw; i++) {
            ui_->drawText(col_ + 1, row_ + 1 + i, lines_[i], fgColor_, bgColor_);
        }
    } else {
        // Draw without border (fill background)
        ui_->fillGridRect(col_, row_, width_, height_, bgColor_);

        // Draw lines
        int maxLines = height_;
        int linesToDraw = (lineCount_ < maxLines) ? lineCount_ : maxLines;

        for (int i = 0; i < linesToDraw; i++) {
            ui_->drawText(col_, row_ + i, lines_[i], fgColor_, bgColor_);
        }
    }
}

void InfoPanel::clear() {
    lineCount_ = 0;
    ui_->fillGridRect(col_, row_, width_, height_, DOS_BLUE);
}

// Convenience methods for common panel types
void InfoPanel::showReminder(const char* text) {
    setColors(DOS_BLACK, DOS_YELLOW);
    setText(text);
}

void InfoPanel::showStatus(const char* text) {
    setColors(DOS_WHITE, DOS_BLUE);
    setText(text);
}

void InfoPanel::showWarning(const char* text) {
    setColors(DOS_BLACK, DOS_BROWN);
    setText(text);
}

void InfoPanel::showError(const char* text) {
    setColors(DOS_WHITE, DOS_RED);
    setText(text);
}

// Word-wrap text into lines
void InfoPanel::wrapText(const char* text) {
    if (!text) {
        lineCount_ = 0;
        return;
    }

    lineCount_ = 0;
    uint8_t messageLen = strlen(text);
    uint8_t pos = 0;

    // Calculate effective line width (accounting for border)
    int lineWidth = border_ ? (width_ - 2) : width_;
    if (lineWidth < 1) lineWidth = 1;
    if (lineWidth > 127) lineWidth = 127;

    int maxLines = border_ ? (height_ - 2) : height_;
    if (maxLines < 1) maxLines = 1;
    if (maxLines > 8) maxLines = 8;

    while (pos < messageLen && lineCount_ < maxLines) {
        uint8_t lineEnd = pos;
        uint8_t lastSpace = pos;
        bool foundNewline = false;

        // Find line break point
        while ((lineEnd - pos) < lineWidth && lineEnd < messageLen) {
            if (text[lineEnd] == '\n') {
                foundNewline = true;
                break;
            }
            if (text[lineEnd] == ' ') {
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
        if (lineLen > 127) lineLen = 127;

        strncpy(lines_[lineCount_], text + pos, lineLen);
        lines_[lineCount_][lineLen] = '\0';

        // Trim leading/trailing spaces
        char* line = lines_[lineCount_];
        while (*line == ' ') line++;
        if (line != lines_[lineCount_]) {
            memmove(lines_[lineCount_], line, strlen(line) + 1);
        }

        lineCount_++;

        // Move to next line
        if (foundNewline) {
            pos = lineEnd + 1;
        } else {
            pos = lineEnd;
            while (pos < messageLen && text[pos] == ' ') {
                pos++;
            }
        }
    }
}
