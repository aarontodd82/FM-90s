#ifndef ACTION_CYCLING_SCREEN_BASE_H
#define ACTION_CYCLING_SCREEN_BASE_H

#include "../screen_new.h"
#include <Adafruit_RGBLCDShield.h>

/**
 * ActionCyclingScreenBase - Base class for screens with action cycling
 *
 * Purpose: Provide reusable action cycling pattern for non-list screens
 *
 * Use this when your screen:
 * - Is NOT a list (use ActionableListScreenBase for lists)
 * - Has multiple actions user can cycle through with LEFT/RIGHT
 * - Needs automatic LCD updates when action changes
 *
 * Example Use Cases:
 * - Now Playing screen (Stop, Browse, Vol+, Vol-)
 * - Playback control screen (Play, Pause, Next, Prev)
 * - Any screen with a "toolbar" of actions
 *
 * Derived classes must implement:
 * - getActions() - Return array of available actions
 * - getActionCount() - Return number of actions
 * - onActionExecuted() - Handle when user presses SELECT
 * - draw() - Draw the screen content
 *
 * Optionally override:
 * - updateLCD() - Custom LCD display (default shows action description)
 * - onUp() / onDown() - Handle UP/DOWN if needed (default: does nothing)
 *
 * Framework provides:
 * - Automatic action cycling (LEFT/RIGHT)
 * - Automatic LCD updates
 * - Action execution (SELECT)
 * - Button handling via onButton()
 */
class ActionCyclingScreenBase : public Screen {
public:
    /**
     * Action definition
     */
    struct Action {
        const char* label;        // Action label ("Stop", "Browse", etc.)
        const char* description;  // Description for LCD/tooltip
        int actionID;             // Action identifier for onActionExecuted()
    };

protected:
    int currentActionIndex_;  // Currently selected action

public:
    ActionCyclingScreenBase(ScreenContext* context)
        : Screen(context)
        , currentActionIndex_(0)
    {}

    // ============================================
    // PURE VIRTUAL - Must implement in derived class
    // ============================================

    /**
     * Get the array of actions
     * @return Pointer to action array
     */
    virtual const Action* getActions() = 0;

    /**
     * Get the number of actions
     * @return Action count
     */
    virtual int getActionCount() = 0;

    /**
     * Called when user executes an action (presses SELECT)
     * @param actionIndex - Index of the action in the actions array
     * @param actionID - The actionID from the Action struct
     * @return ScreenResult indicating what to do next
     */
    virtual ScreenResult onActionExecuted(int actionIndex, int actionID) = 0;

    // ============================================
    // OPTIONAL OVERRIDES
    // ============================================

    /**
     * Called when UP is pressed
     * Default: does nothing
     */
    virtual ScreenResult onUp() {
        return ScreenResult::stay();
    }

    /**
     * Called when DOWN is pressed
     * Default: does nothing
     */
    virtual ScreenResult onDown() {
        return ScreenResult::stay();
    }

    /**
     * Update LCD display
     * Default: Show action description and button legend
     */
    void updateLCD() override {
        if (!context_->lcdManager) return;

        int count = getActionCount();
        if (count == 0 || currentActionIndex_ < 0 || currentActionIndex_ >= count) {
            context_->lcdManager->setLine(0, "No actions");
            context_->lcdManager->clearLine(1);
            return;
        }

        const Action* actions = getActions();
        const Action& action = actions[currentActionIndex_];

        // Line 1: Action description
        context_->lcdManager->setLine(0, action.description);

        // Line 2: Button legend (build with custom characters)
        char line2[17];
        snprintf(line2, sizeof(line2), "\x7F\x7E""Act \x00%s", action.label);
        context_->lcdManager->setLine(1, line2);
    }

    // ============================================
    // BUTTON HANDLING (implements Screen::onButton)
    // ============================================

    ScreenResult onButton(uint8_t button) override {
        int count = getActionCount();

        switch (button) {
            case BUTTON_LEFT:
                // Previous action
                if (count > 0) {
                    currentActionIndex_--;
                    if (currentActionIndex_ < 0) {
                        currentActionIndex_ = count - 1;  // Wrap to last
                    }
                    updateLCD();
                }
                return ScreenResult::stay();

            case BUTTON_RIGHT:
                // Next action
                if (count > 0) {
                    currentActionIndex_++;
                    if (currentActionIndex_ >= count) {
                        currentActionIndex_ = 0;  // Wrap to first
                    }
                    updateLCD();
                }
                return ScreenResult::stay();

            case BUTTON_UP:
                return onUp();

            case BUTTON_DOWN:
                return onDown();

            case BUTTON_SELECT: {
                // Execute current action
                if (count == 0 || currentActionIndex_ < 0 || currentActionIndex_ >= count) {
                    return ScreenResult::stay();
                }

                const Action* actions = getActions();
                return onActionExecuted(currentActionIndex_, actions[currentActionIndex_].actionID);
            }

            default:
                return ScreenResult::stay();
        }
    }

    // ============================================
    // HELPER METHODS
    // ============================================

    /**
     * Get the currently selected action index
     */
    int getCurrentActionIndex() const {
        return currentActionIndex_;
    }

    /**
     * Set the current action index (useful for initialization)
     */
    void setCurrentActionIndex(int index) {
        int count = getActionCount();
        if (index >= 0 && index < count) {
            currentActionIndex_ = index;
            updateLCD();
        }
    }
};

#endif // ACTION_CYCLING_SCREEN_BASE_H
