#include "async_operation.h"
#include "progress_indicator.h"
#include "../../retro_ui.h"
#include "../../dos_colors.h"
#include <Arduino.h>

// ============================================
// CONSTRUCTOR
// ============================================

AsyncOperation::AsyncOperation(const char* label, unsigned long timeoutMs)
    : state_(STATE_IDLE),
      startTime_(0),
      timeoutMs_(timeoutMs),
      progressIndicator_(nullptr) {

    strncpy(label_, label, sizeof(label_) - 1);
    label_[sizeof(label_) - 1] = '\0';
    errorMessage_[0] = '\0';
}

// ============================================
// LIFECYCLE
// ============================================

void AsyncOperation::start() {
    if (state_ != STATE_IDLE) {
        // // Serial.print("[AsyncOperation] WARNING: Starting operation that's not IDLE (current state: ");
        // // Serial.print(state_);
        // // Serial.println(")");
        return;
    }

    state_ = STATE_RUNNING;
    startTime_ = millis();
    errorMessage_[0] = '\0';

    // // Serial.print("[AsyncOperation] Started: ");
    // // Serial.print(label_);
    // // Serial.print(" (timeout: ");
    // // Serial.print(timeoutMs_);
    // // Serial.println("ms)");
}

void AsyncOperation::cancel() {
    if (state_ != STATE_RUNNING) {
        // // Serial.println("[AsyncOperation] WARNING: Canceling operation that's not RUNNING");
        return;
    }

    // // Serial.print("[AsyncOperation] Canceled: ");
    // // Serial.println(label_);

    onCancel();

    state_ = STATE_FAILED;
    strncpy(errorMessage_, "Operation canceled by user", sizeof(errorMessage_) - 1);
    errorMessage_[sizeof(errorMessage_) - 1] = '\0';
}

void AsyncOperation::update() {
    if (state_ != STATE_RUNNING) {
        return;
    }

    // Check for timeout
    unsigned long elapsed = millis() - startTime_;
    if (elapsed >= timeoutMs_) {
        // // Serial.print("[AsyncOperation] TIMEOUT: ");
        // // Serial.print(label_);
        // // Serial.print(" (");
        // // Serial.print(elapsed);
        // // Serial.println("ms)");

        state_ = STATE_TIMEOUT;
        snprintf(errorMessage_, sizeof(errorMessage_),
                 "Operation timed out after %lu seconds", timeoutMs_ / 1000);

        onFailed();
        return;
    }

    // Update progress indicator if attached
    if (progressIndicator_) {
        progressIndicator_->setTime(elapsed, timeoutMs_);
        progressIndicator_->update();
    }

    // Check if operation is complete
    if (poll()) {
        // // Serial.print("[AsyncOperation] Completed: ");
        // // Serial.print(label_);
        // // Serial.print(" (");
        // // Serial.print(elapsed);
        // // Serial.println("ms)");

        state_ = STATE_COMPLETED;
        onComplete();
    }
}

// ============================================
// PROGRESS
// ============================================

float AsyncOperation::getProgress() const {
    if (state_ == STATE_IDLE) {
        return 0.0f;
    }
    if (state_ >= STATE_COMPLETED) {
        return 1.0f;
    }

    unsigned long elapsed = millis() - startTime_;
    if (timeoutMs_ == 0) {
        return 0.0f;
    }

    float progress = (float)elapsed / (float)timeoutMs_;
    return (progress > 1.0f) ? 1.0f : progress;
}

unsigned long AsyncOperation::getElapsedMs() const {
    if (state_ == STATE_IDLE) {
        return 0;
    }
    if (state_ >= STATE_COMPLETED) {
        // Return final duration
        return timeoutMs_;  // Could store actual completion time
    }
    return millis() - startTime_;
}

unsigned long AsyncOperation::getRemainingMs() const {
    if (state_ != STATE_RUNNING) {
        return 0;
    }

    unsigned long elapsed = millis() - startTime_;
    if (elapsed >= timeoutMs_) {
        return 0;
    }

    return timeoutMs_ - elapsed;
}

// ============================================
// ERROR HANDLING
// ============================================

void AsyncOperation::setError(const char* errorMsg) {
    if (state_ == STATE_RUNNING) {
        // // Serial.print("[AsyncOperation] ERROR: ");
        // // Serial.print(label_);
        // // Serial.print(" - ");
        // // Serial.println(errorMsg);

        state_ = STATE_FAILED;
        strncpy(errorMessage_, errorMsg, sizeof(errorMessage_) - 1);
        errorMessage_[sizeof(errorMessage_) - 1] = '\0';

        onFailed();
    }
}

// ============================================
// VISUAL FEEDBACK
// ============================================

void AsyncOperation::attachProgressIndicator(ProgressIndicator* indicator) {
    progressIndicator_ = indicator;

    if (indicator) {
        indicator->setLabel(label_);
        indicator->setStyle(ProgressIndicator::STYLE_BAR_TIME);
    }
}

void AsyncOperation::drawProgress(RetroUI* ui, int row) {
    if (!ui) return;

    // Draw operation label
    ui->drawText(5, row, label_, DOS_WHITE, DOS_BLUE);

    // Draw progress bar manually if no ProgressIndicator attached
    if (!progressIndicator_) {
        float progress = getProgress();
        int barWidth = 40;
        int filledWidth = (int)(progress * barWidth);

        // Draw bar at row + 1
        char bar[64];
        bar[0] = '[';
        for (int i = 0; i < barWidth; i++) {
            bar[i + 1] = (i < filledWidth) ? '#' : '-';
        }
        bar[barWidth + 1] = ']';
        bar[barWidth + 2] = '\0';

        ui->drawText(5, row + 1, bar, DOS_CYAN, DOS_BLUE);

        // Draw elapsed/total time
        unsigned long elapsedSec = getElapsedMs() / 1000;
        unsigned long totalSec = timeoutMs_ / 1000;

        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "%lu/%lus", elapsedSec, totalSec);
        ui->drawText(50, row + 1, timeStr, DOS_YELLOW, DOS_BLUE);
    } else {
        // ProgressIndicator handles drawing
        progressIndicator_->draw();
    }
}
