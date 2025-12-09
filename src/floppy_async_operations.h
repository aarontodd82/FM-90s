#ifndef FLOPPY_ASYNC_OPERATIONS_H
#define FLOPPY_ASYNC_OPERATIONS_H

#include "ui/framework/async_operation.h"
#include "floppy_manager.h"

/**
 * FloppyTransferOperation - Async wrapper for XModem floppy file transfers
 *
 * Features:
 * - Non-blocking XModem transfer from Arduino Nano floppy shield
 * - Real-time progress updates
 * - Automatic timeout handling
 * - Event firing for transfer progress
 *
 * Usage:
 *   FloppyTransferOperation* transfer = new FloppyTransferOperation(
 *       floppyManager, eventManager, "MYFILE.MID");
 *   transfer->start();
 *
 *   void loop() {
 *       transfer->update();
 *       if (transfer->isDone()) {
 *           if (transfer->isSuccess()) {
 *               const char* destPath = transfer->getDestinationPath();
 *               // File is now in /TEMP on SD card
 *           } else {
 // *               // Serial.println(transfer->getErrorMessage());
 *           }
 *           delete transfer;
 *       }
 *   }
 */
class FloppyTransferOperation : public AsyncOperation {
public:
    /**
     * Create a floppy transfer operation
     * @param floppyMgr - FloppyManager instance
     * @param eventMgr - EventManager for progress events (optional)
     * @param filename - Name of file on floppy to transfer
     * @param timeoutMs - Transfer timeout (default: 120 seconds for large files)
     */
    FloppyTransferOperation(FloppyManager* floppyMgr,
                           EventManager* eventMgr,
                           const char* filename,
                           unsigned long timeoutMs = 120000)
        : AsyncOperation("Transferring from floppy", timeoutMs),
          floppyManager_(floppyMgr),
          eventManager_(eventMgr),
          lastProgressPercent_(0) {
        strncpy(filename_, filename, sizeof(filename_) - 1);
        filename_[sizeof(filename_) - 1] = '\0';

        // Update label with filename
        snprintf(label_, sizeof(label_), "Transferring %s", filename);

        // Destination path is /TEMP/<filename>
        snprintf(destinationPath_, sizeof(destinationPath_), "/TEMP/%s", filename);
    }

    /**
     * Get destination path on SD card (only valid after successful transfer)
     */
    const char* getDestinationPath() const {
        return destinationPath_;
    }

    void start() override {
        // Fire event
        if (eventManager_) {
            eventManager_->fire(EventManager::EVENT_FLOPPY_TRANSFER_STARTED);
        }

        // Initiate XModem transfer
        bool started = floppyManager_->getFile(filename_);
        if (!started) {
            setError("Failed to initiate floppy transfer");
            if (eventManager_) {
                eventManager_->fireStr(EventManager::EVENT_FLOPPY_TRANSFER_FAILED,
                                      "Failed to start transfer");
            }
            return;
        }

        AsyncOperation::start();

        // // Serial.print("[FloppyTransfer] Started: ");
        // // Serial.println(filename_);
    }

protected:
    bool poll() override {
        // Check if transfer is complete
        if (floppyManager_->isTransferComplete()) {
            return true;
        }

        // Check if transfer failed
        if (floppyManager_->hasTransferError()) {
            setError(floppyManager_->getTransferError());
            if (eventManager_) {
                eventManager_->fireStr(EventManager::EVENT_FLOPPY_TRANSFER_FAILED,
                                      getErrorMessage());
            }
            return true;  // Operation is "done" (with failure)
        }

        // Fire progress events (throttled to 5% increments to avoid spam)
        int currentProgress = floppyManager_->getTransferProgress();
        if (currentProgress >= lastProgressPercent_ + 5) {
            lastProgressPercent_ = currentProgress;
            if (eventManager_) {
                eventManager_->fireInt(EventManager::EVENT_FLOPPY_TRANSFER_PROGRESS,
                                      currentProgress);
            }
        }

        return false;  // Still transferring
    }

    /**
     * Override getProgress to use actual transfer progress
     * instead of time-based progress
     */
    float getProgress() const {
        if (state_ == STATE_IDLE) {
            return 0.0f;
        }
        if (state_ >= STATE_COMPLETED) {
            return 1.0f;
        }

        int progress = floppyManager_->getTransferProgress();
        return (float)progress / 100.0f;
    }

    void onComplete() override {
        // // Serial.print("[FloppyTransfer] Completed: ");
        // // Serial.print(filename_);
        // // Serial.print(" -> ");
        // // Serial.println(destinationPath_);

        if (eventManager_) {
            eventManager_->fire(EventManager::EVENT_FLOPPY_TRANSFER_COMPLETE);
        }
    }

    void onFailed() override {
        // // Serial.print("[FloppyTransfer] Failed: ");
        // // Serial.print(filename_);
        // // Serial.print(" - ");
        // // Serial.println(getErrorMessage());

        // Cancel the transfer on the floppy side
        floppyManager_->cancelTransfer();
    }

    void onCancel() override {
        // // Serial.print("[FloppyTransfer] Canceled: ");
        // // Serial.println(filename_);

        // Cancel the transfer
        floppyManager_->cancelTransfer();

        if (eventManager_) {
            eventManager_->fireStr(EventManager::EVENT_FLOPPY_TRANSFER_FAILED,
                                  "Transfer canceled by user");
        }
    }

private:
    FloppyManager* floppyManager_;
    EventManager* eventManager_;
    char filename_[64];
    char destinationPath_[128];
    int lastProgressPercent_;
};

#endif // FLOPPY_ASYNC_OPERATIONS_H
