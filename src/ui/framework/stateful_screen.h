#ifndef STATEFUL_SCREEN_H
#define STATEFUL_SCREEN_H

#include "../screen_new.h"
#include <Arduino.h>

/**
 * StatefulScreen - Template for screens with explicit state machines
 *
 * Features:
 * - Type-safe state enum
 * - Automatic state transition logging
 * - State enter/exit hooks
 * - State-specific rendering
 * - State history tracking
 * - Optional state timeout detection
 *
 * Usage:
 *   enum MyState { IDLE, LOADING, READY, ERROR };
 *
 *   class MyScreen : public StatefulScreen<MyState> {
 *   public:
 *       MyScreen(ScreenContext* ctx) : StatefulScreen(ctx, IDLE) {}
 *
 *       void onStateEnter(MyState state) override {
 *           switch(state) {
 *               case LOADING: startLoadingAnimation(); break;
 *               case READY: stopLoadingAnimation(); break;
 *           }
 *       }
 *
 *       void drawForState(MyState state) override {
 *           switch(state) {
 *               case IDLE: drawIdleScreen(); break;
 *               case LOADING: drawLoadingScreen(); break;
 *           }
 *       }
 *   };
 */
template<typename StateEnum>
class StatefulScreen : public Screen {
protected:
    StateEnum currentState_;
    StateEnum previousState_;
    unsigned long stateEnteredTime_;
    bool stateChanged_;

    // State history (last 8 states for debugging)
    static const int STATE_HISTORY_SIZE = 8;
    StateEnum stateHistory_[STATE_HISTORY_SIZE];
    int stateHistoryIndex_;

public:
    /**
     * Create a stateful screen
     * @param context - Screen context for dependency injection
     * @param initialState - Starting state
     */
    StatefulScreen(ScreenContext* context, StateEnum initialState)
        : Screen(context),
          currentState_(initialState),
          previousState_(initialState),
          stateEnteredTime_(0),
          stateChanged_(false),
          stateHistoryIndex_(0) {

        // Initialize state history
        for (int i = 0; i < STATE_HISTORY_SIZE; i++) {
            stateHistory_[i] = initialState;
        }
    }

    virtual ~StatefulScreen() {}

    // ============================================
    // STATE QUERIES
    // ============================================

    /**
     * Get current state
     */
    StateEnum getState() const { return currentState_; }

    /**
     * Get previous state
     */
    StateEnum getPreviousState() const { return previousState_; }

    /**
     * Get time elapsed in current state (in milliseconds)
     */
    unsigned long getStateElapsedMs() const {
        return millis() - stateEnteredTime_;
    }

    /**
     * Check if currently in a specific state
     */
    bool isInState(StateEnum state) const {
        return currentState_ == state;
    }

    /**
     * Check if just transitioned to current state this frame
     */
    bool justEnteredState() const {
        return stateChanged_;
    }

    // ============================================
    // STATE TRANSITIONS
    // ============================================

    /**
     * Transition to a new state
     * Calls onStateExit(), updates state, calls onStateEnter()
     * Automatically logs transition and requests redraw
     */
    void transitionTo(StateEnum newState) {
        if (newState != currentState_) {
            // // Serial.print("[State] ");
            // // Serial.print(getStateName(currentState_));
            // // Serial.print(" -> ");
            // // Serial.print(getStateName(newState));
            // // Serial.print(" (elapsed: ");
            // // Serial.print(getStateElapsedMs());
            // // Serial.println("ms)");

            // Call exit hook for current state
            onStateExit(currentState_);

            // Update state
            previousState_ = currentState_;
            currentState_ = newState;
            stateEnteredTime_ = millis();
            stateChanged_ = true;

            // Record in history
            addToHistory(newState);

            // Call enter hook for new state
            onStateEnter(currentState_);

            // Request redraw
            requestRedraw();
        }
    }

    /**
     * Return to previous state
     */
    void returnToPreviousState() {
        transitionTo(previousState_);
    }

    // ============================================
    // STATE HISTORY
    // ============================================

    /**
     * Get state from history
     * @param index - 0 = most recent, 1 = one before, etc.
     */
    StateEnum getStateFromHistory(int index) const {
        if (index < 0 || index >= STATE_HISTORY_SIZE) {
            return currentState_;
        }

        int historyIdx = (stateHistoryIndex_ - index - 1 + STATE_HISTORY_SIZE) % STATE_HISTORY_SIZE;
        return stateHistory_[historyIdx];
    }

    /**
     * Print state history to serial (for debugging)
     */
    void printStateHistory() const {
        // // Serial.println("[State History] (most recent first):");
        for (int i = 0; i < STATE_HISTORY_SIZE; i++) {
            StateEnum state = getStateFromHistory(i);
            // // Serial.print("  [");
            // // Serial.print(i);
            // // Serial.print("] ");
            // // Serial.println(getStateName(state));
        }
    }

    // ============================================
    // SCREEN BASE OVERRIDES
    // ============================================

    void draw() override {
        drawForState(currentState_);
    }

    void update() override {
        // Auto-draw on state change
        if (stateChanged_) {
            draw();
            stateChanged_ = false;
        }

        // Call state-specific update
        updateForState(currentState_);

        // Call base update
        Screen::update();
    }

protected:
    // ============================================
    // PURE VIRTUAL METHODS (MUST OVERRIDE)
    // ============================================

    /**
     * Called when entering a new state
     * Use for: starting async operations, resetting timers, showing UI, etc.
     */
    virtual void onStateEnter(StateEnum state) = 0;

    /**
     * Called when exiting a state
     * Use for: cleanup, canceling operations, hiding UI, etc.
     */
    virtual void onStateExit(StateEnum state) = 0;

    /**
     * Draw the screen for a specific state
     */
    virtual void drawForState(StateEnum state) = 0;

    // ============================================
    // OPTIONAL VIRTUAL METHODS (CAN OVERRIDE)
    // ============================================

    /**
     * Update logic for a specific state (optional)
     * Called every frame while in this state
     *
     * Use for:
     * - Periodic updates (check elapsed time since last update)
     * - Polling external state (playback position, file progress)
     * - Animation updates
     * - Timeout detection (use checkStateTimeout())
     *
     * REAL-TIME UPDATE PATTERN:
     * For screens that need periodic updates (e.g., now playing screen showing
     * playback progress), use a throttled update pattern:
     *
     *   void updateForState(MyState state) override {
     *       if (state == STATE_PLAYING) {
     *           unsigned long now = millis();
     *           if (now - lastProgressUpdate_ >= PROGRESS_UPDATE_INTERVAL) {
     *               // Update playback progress display
     *               updatePlaybackProgress();
     *               lastProgressUpdate_ = now;
     *           }
     *
     *           if (now - lastRegisterUpdate_ >= REGISTER_UPDATE_INTERVAL) {
     *               // Update OPL register visualization (faster rate)
     *               updateRegisterDisplay();
     *               lastRegisterUpdate_ = now;
     *           }
     *       }
     *   }
     *
     * Example intervals:
     * - Progress bar: 1000ms (1 Hz) - low update rate
     * - Register visualization: 100ms (10 Hz) - higher rate
     * - Animations: 50ms (20 Hz) - smooth animations
     */
    virtual void updateForState(StateEnum state) {
        (void)state;  // Unused by default
    }

    /**
     * Get human-readable state name for logging (optional)
     * Override to provide better debug output
     */
    virtual const char* getStateName(StateEnum state) const {
        (void)state;
        return "UNKNOWN";
    }

    /**
     * Validate state transition (optional)
     * Override to enforce valid state transitions
     * @return true if transition is allowed, false to block it
     */
    virtual bool isValidTransition(StateEnum from, StateEnum to) const {
        (void)from;
        (void)to;
        return true;  // Allow all transitions by default
    }

    /**
     * Called when a state times out (optional)
     * Override and call checkStateTimeout() in updateForState() to use
     */
    virtual void onStateTimeout(StateEnum state) {
        (void)state;
    }

    /**
     * Check if current state has exceeded a timeout
     * Call this in updateForState() to detect timeouts
     */
    bool checkStateTimeout(unsigned long timeoutMs) {
        if (getStateElapsedMs() >= timeoutMs) {
            onStateTimeout(currentState_);
            return true;
        }
        return false;
    }

private:
    /**
     * Add state to circular history buffer
     */
    void addToHistory(StateEnum state) {
        stateHistory_[stateHistoryIndex_] = state;
        stateHistoryIndex_ = (stateHistoryIndex_ + 1) % STATE_HISTORY_SIZE;
    }
};

#endif // STATEFUL_SCREEN_H
