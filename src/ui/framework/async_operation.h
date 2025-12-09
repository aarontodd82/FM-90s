#ifndef ASYNC_OPERATION_H
#define ASYNC_OPERATION_H

#include <Arduino.h>

// Forward declaration to avoid including progress_indicator.h
class ProgressIndicator;
class RetroUI;

/**
 * AsyncOperation - Base class for long-running operations
 *
 * Provides:
 * - State tracking (idle, running, completed, failed, timeout)
 * - Automatic progress updates
 * - Timeout handling
 * - Error management
 * - Visual feedback integration
 *
 * MEMORY MANAGEMENT:
 * - AsyncOperation instances are typically heap-allocated (new)
 * - The SCREEN is responsible for deleting the operation when done
 * - Always check isDone() before deleting to avoid use-after-free
 * - Consider using a member variable to track ownership
 *
 * Usage Pattern (Heap-allocated):
 *   // In screen's onCreate() or onEnter()
 *   currentOperation_ = new BluetoothScanOperation(btManager);
 *   currentOperation_->attachProgressIndicator(&progressBar_);
 *   currentOperation_->start();
 *
 *   // In screen's update()
 *   if (currentOperation_) {
 *       currentOperation_->update();
 *       if (currentOperation_->isDone()) {
 *           if (currentOperation_->isSuccess()) {
 *               // Handle success
 *           } else {
 *               // Handle failure
 *           }
 *           delete currentOperation_;
 *           currentOperation_ = nullptr;
 *       }
 *   }
 *
 *   // In screen's onDestroy()
 *   if (currentOperation_) {
 *       delete currentOperation_;
 *       currentOperation_ = nullptr;
 *   }
 *
 * Usage Pattern (Stack-allocated for short operations):
 *   ScanOperation scan("Scanning", 5000);
 *   scan.start();
 *   while (!scan.isDone()) {
 *       scan.update();
 *       delay(100);
 *   }
 *   // Automatically deleted when going out of scope
 */
class AsyncOperation {
public:
    enum State {
        STATE_IDLE,        // Not started yet
        STATE_RUNNING,     // Currently executing
        STATE_COMPLETED,   // Finished successfully
        STATE_FAILED,      // Failed with error
        STATE_TIMEOUT      // Timed out
    };

    /**
     * Create an async operation
     * @param label - Human-readable description (for progress display)
     * @param timeoutMs - Timeout in milliseconds (default: 30 seconds)
     */
    AsyncOperation(const char* label, unsigned long timeoutMs = 30000);
    virtual ~AsyncOperation() {}

    // ============================================
    // LIFECYCLE
    // ============================================

    /**
     * Start the operation
     * Transitions from IDLE to RUNNING
     */
    virtual void start();

    /**
     * Cancel the operation
     * Calls onCancel() hook and transitions to FAILED
     */
    virtual void cancel();

    /**
     * Update the operation state
     * Call this every loop iteration while operation is running
     * Checks for completion via poll() and handles timeouts
     */
    virtual void update();

    // ============================================
    // STATE QUERIES
    // ============================================

    State getState() const { return state_; }
    bool isRunning() const { return state_ == STATE_RUNNING; }
    bool isDone() const { return state_ >= STATE_COMPLETED; }
    bool isSuccess() const { return state_ == STATE_COMPLETED; }
    bool isFailed() const { return state_ == STATE_FAILED || state_ == STATE_TIMEOUT; }

    // ============================================
    // PROGRESS
    // ============================================

    /**
     * Get progress as 0.0 to 1.0
     * Based on elapsed time vs timeout
     */
    float getProgress() const;

    /**
     * Get elapsed time since start (in milliseconds)
     */
    unsigned long getElapsedMs() const;

    /**
     * Get remaining time until timeout (in milliseconds)
     */
    unsigned long getRemainingMs() const;

    /**
     * Get the operation label
     */
    const char* getLabel() const { return label_; }

    // ============================================
    // ERROR HANDLING
    // ============================================

    /**
     * Set error message (transitions to FAILED state)
     * @param errorMsg - Human-readable error message
     */
    void setError(const char* errorMsg);

    /**
     * Get error message (if any)
     */
    const char* getErrorMessage() const { return errorMessage_; }

    // ============================================
    // VISUAL FEEDBACK
    // ============================================

    /**
     * Attach a ProgressIndicator for automatic updates
     * The indicator will be updated during update() calls
     * @param indicator - ProgressIndicator instance (caller must keep alive)
     */
    void attachProgressIndicator(ProgressIndicator* indicator);

    /**
     * Draw progress to screen (convenience method)
     * @param ui - RetroUI instance
     * @param row - Row to draw at
     */
    void drawProgress(RetroUI* ui, int row);

protected:
    /**
     * Override this method to check if operation is complete
     * Called every update() while in RUNNING state
     * @return true if operation finished successfully, false if still running
     */
    virtual bool poll() = 0;

    /**
     * Override to perform cleanup on successful completion
     * Called once when poll() returns true
     */
    virtual void onComplete() {}

    /**
     * Override to perform cleanup on failure
     * Called once when timeout occurs or setError() is called
     */
    virtual void onFailed() {}

    /**
     * Override to perform custom cancel logic
     * Called once when cancel() is called
     */
    virtual void onCancel() {}

    // State
    State state_;
    char label_[64];
    unsigned long startTime_;
    unsigned long timeoutMs_;
    char errorMessage_[128];

    // Visual feedback
    ProgressIndicator* progressIndicator_;
};

#endif // ASYNC_OPERATION_H
