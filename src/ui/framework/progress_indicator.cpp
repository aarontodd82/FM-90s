#include "progress_indicator.h"
#include "../../dos_colors.h"
#include <Arduino.h>

ProgressIndicator::ProgressIndicator(RetroUI* ui, int col, int row, int width)
    : ui_(ui),
      col_(col),
      row_(row),
      width_(width),
      style_(STYLE_BAR),
      progress_(0.0f),
      elapsedMs_(0),
      totalMs_(0),
      barColor_(DOS_BRIGHT_CYAN),
      lastSpinnerUpdate_(0),
      spinnerFrame_(0) {
    label_[0] = '\0';
}

void ProgressIndicator::setProgress(float progress) {
    progress_ = progress;
    if (progress_ < 0.0f) progress_ = 0.0f;
    if (progress_ > 1.0f) progress_ = 1.0f;

    // Calculate elapsed/total from progress if not set
    if (totalMs_ > 0) {
        elapsedMs_ = (unsigned long)(progress_ * totalMs_);
    }
}

void ProgressIndicator::setTime(unsigned long elapsedMs, unsigned long totalMs) {
    elapsedMs_ = elapsedMs;
    totalMs_ = totalMs;

    // Calculate progress from time
    if (totalMs_ > 0) {
        progress_ = (float)elapsedMs_ / (float)totalMs_;
        if (progress_ > 1.0f) progress_ = 1.0f;
    }
}

void ProgressIndicator::setLabel(const char* label) {
    if (label) {
        strncpy(label_, label, sizeof(label_) - 1);
        label_[sizeof(label_) - 1] = '\0';
    } else {
        label_[0] = '\0';
    }
}

void ProgressIndicator::setStyle(Style style) {
    style_ = style;
}

void ProgressIndicator::setColor(uint16_t color) {
    barColor_ = color;
}

void ProgressIndicator::draw() {
    switch (style_) {
        case STYLE_BAR:
            drawBar();
            break;
        case STYLE_BAR_TIME:
            drawBarTime();
            break;
        case STYLE_PERCENTAGE:
            drawPercentage();
            break;
        case STYLE_SPINNER:
            drawSpinner();
            break;
    }
}

void ProgressIndicator::update() {
    // Animate spinner if using spinner style
    if (style_ == STYLE_SPINNER) {
        unsigned long now = millis();
        if (now - lastSpinnerUpdate_ >= 200) {  // Update every 200ms
            spinnerFrame_ = (spinnerFrame_ + 1) % 4;
            lastSpinnerUpdate_ = now;
            drawSpinner();
        }
    }
}

void ProgressIndicator::clear() {
    ui_->fillGridRect(col_, row_, width_, 1, DOS_BLUE);
}

// Draw progress bar with percentage
void ProgressIndicator::drawBar() {
    int currentCol = col_;

    // Draw label if present
    if (label_[0] != '\0') {
        ui_->drawText(currentCol, row_, label_, DOS_WHITE, DOS_BLUE);
        currentCol += strlen(label_) + 1;  // Move past label
    }

    // Calculate bar width (leave room for " 100%")
    int barWidth = width_ - (currentCol - col_) - 5;  // 5 chars for " 100%"
    if (barWidth < 10) barWidth = 10;

    // Draw the progress bar
    ui_->drawProgressBar(currentCol, row_, barWidth, progress_, barColor_, DOS_BLUE);

    // Draw percentage text
    char percentText[6];
    snprintf(percentText, sizeof(percentText), "%3d%%", (int)(progress_ * 100));
    ui_->drawText(currentCol + barWidth + 1, row_, percentText, DOS_YELLOW, DOS_BLUE);
}

// Draw progress bar with time display
void ProgressIndicator::drawBarTime() {
    int currentCol = col_;

    // Draw label if present
    if (label_[0] != '\0') {
        ui_->drawText(currentCol, row_, label_, DOS_WHITE, DOS_BLUE);
        currentCol += strlen(label_) + 1;
    }

    // Format time strings
    char elapsedStr[16];
    char totalStr[16];
    formatTime(elapsedMs_, elapsedStr, sizeof(elapsedStr));
    formatTime(totalMs_, totalStr, sizeof(totalStr));

    // Calculate time display width (e.g., " 12s/20s" = ~8 chars)
    char timeText[32];
    snprintf(timeText, sizeof(timeText), " %s/%s", elapsedStr, totalStr);
    int timeWidth = strlen(timeText);

    // Calculate bar width
    int barWidth = width_ - (currentCol - col_) - timeWidth;
    if (barWidth < 10) barWidth = 10;

    // Draw the progress bar
    ui_->drawProgressBar(currentCol, row_, barWidth, progress_, barColor_, DOS_BLUE);

    // Draw time text
    ui_->drawText(currentCol + barWidth, row_, timeText, DOS_YELLOW, DOS_BLUE);
}

// Draw just percentage
void ProgressIndicator::drawPercentage() {
    int currentCol = col_;

    // Draw label if present
    if (label_[0] != '\0') {
        ui_->drawText(currentCol, row_, label_, DOS_WHITE, DOS_BLUE);
        currentCol += strlen(label_) + 1;
    }

    // Draw percentage
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%3d%%", (int)(progress_ * 100));
    ui_->drawText(currentCol, row_, percentText, DOS_YELLOW, DOS_BLUE);
}

// Draw spinning indicator for indeterminate operations
void ProgressIndicator::drawSpinner() {
    int currentCol = col_;

    // Draw label if present
    if (label_[0] != '\0') {
        ui_->drawText(currentCol, row_, label_, DOS_WHITE, DOS_BLUE);
        currentCol += strlen(label_) + 1;
    }

    // Spinner characters (classic DOS: | / - \)
    const char* spinnerChars[] = { "|", "/", "-", "\\" };
    char spinnerText[4];
    snprintf(spinnerText, sizeof(spinnerText), "[%s]", spinnerChars[spinnerFrame_]);

    ui_->drawText(currentCol, row_, spinnerText, barColor_, DOS_BLUE);
}

// Format milliseconds as human-readable time
void ProgressIndicator::formatTime(unsigned long ms, char* buffer, size_t bufferSize) {
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;

    if (hours > 0) {
        // Format as "Xh Ym"
        snprintf(buffer, bufferSize, "%luh%02lum", hours, minutes % 60);
    } else if (minutes > 0) {
        // Format as "Xm Ys"
        snprintf(buffer, bufferSize, "%lum%02lus", minutes, seconds % 60);
    } else {
        // Format as "Xs"
        snprintf(buffer, bufferSize, "%lus", seconds);
    }
}
