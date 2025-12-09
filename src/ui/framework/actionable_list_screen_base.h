#ifndef ACTIONABLE_LIST_SCREEN_BASE_H
#define ACTIONABLE_LIST_SCREEN_BASE_H

#include "list_screen_base.h"
#include <Arduino.h>

/**
 * ActionableListScreenBase - List screen with multi-action support
 *
 * Extends ListScreenBase to add action cycling functionality.
 * Items can have multiple actions that user cycles through with LEFT/RIGHT.
 *
 * Use cases:
 * - File browser: Play, Queue, Add to Playlist, Info
 * - Playlist items: Play, Remove, Move Up, Move Down
 * - Settings: Adjust, Reset, Copy
 *
 * Features:
 * - Multiple actions per item
 * - LEFT/RIGHT to cycle through actions
 * - SELECT to execute current action
 * - Automatic LCD updates with current action
 *
 * Example:
 *   struct ItemAction {
 *       const char* label;        // "Play", "Queue", "Info"
 *       const char* description;  // "Play this file"
 *   };
 *
 *   // Implement getItemActions()
 *   const ItemAction* getItemActions(int index, int& count) override {
 *       if (isFolder(index)) {
 *           static ItemAction folderActions[] = {
 *               {"Open", "Open this folder"},
 *               {"Back", "Go back"}
 *           };
 *           count = 2;
 *           return folderActions;
 *       } else {
 *           static ItemAction fileActions[] = {
 *               {"Play", "Play this file"},
 *               {"Queue", "Add to queue"},
 *               {"Info", "View file info"}
 *           };
 *           count = 3;
 *           return fileActions;
 *       }
 *   }
 *
 *   // Implement onActionExecuted()
 *   ScreenResult onActionExecuted(int itemIndex, int actionIndex) override {
 *       if (actionIndex == 0) {
 *           // Play file
 *           return ScreenResult::navigateTo(SCREEN_NOW_PLAYING);
 *       }
 *       return ScreenResult::stay();
 *   }
 */
class ActionableListScreenBase : public ListScreenBase {
public:
    struct ItemAction {
        const char* label;        // Action label (e.g., "Play", "Queue")
        const char* description;  // Description for LCD/tooltip
    };

protected:
    int currentActionIndex_;  // Current action index for selected item

public:
    /**
     * Create an actionable list screen
     * @param context - Screen context
     * @param visibleCount - Number of visible items
     * @param startRowNum - First row for drawing items
     * @param spacing - Space between items
     */
    ActionableListScreenBase(ScreenContext* context,
                            int visibleCount = 20,
                            int startRowNum = 5,
                            int spacing = 1)
        : ListScreenBase(context, visibleCount, startRowNum, spacing),
          currentActionIndex_(0) {}

    virtual ~ActionableListScreenBase() {}

    // ============================================
    // PURE VIRTUAL - Must implement
    // ============================================

    /**
     * Get available actions for an item
     * @param itemIndex - Index of the item
     * @param count - OUTPUT: Number of actions returned
     * @return Array of actions, or nullptr if no actions
     *
     * NOTE: Return value should be static or member array - don't return local stack!
     */
    virtual const ItemAction* getItemActions(int itemIndex, int& count) = 0;

    /**
     * Called when an action is executed via SELECT
     * @param itemIndex - Index of the item
     * @param actionIndex - Index of the action (0 to count-1)
     * @return ScreenResult for navigation
     */
    virtual ScreenResult onActionExecuted(int itemIndex, int actionIndex) = 0;

    // ============================================
    // OVERRIDE LIST SCREEN BASE
    // ============================================

    /**
     * Called when item selection changes
     * Reset to first action when changing items
     */
    void onEnter() override {
        currentActionIndex_ = 0;
        ListScreenBase::onEnter();
    }

    /**
     * Handle SELECT - execute current action
     */
    ScreenResult onItemSelected(int itemIndex) override {
        int actionCount = 0;
        const ItemAction* actions = getItemActions(itemIndex, actionCount);

        if (!actions || actionCount == 0 || currentActionIndex_ >= actionCount) {
            // No actions available
            return ScreenResult::stay();
        }

        // Execute the current action
        Serial.print("[ActionableList] Executing action ");
        Serial.print(currentActionIndex_);
        Serial.print(" (");
        Serial.print(actions[currentActionIndex_].label);
        Serial.print(") on item ");
        Serial.println(itemIndex);

        return onActionExecuted(itemIndex, currentActionIndex_);
    }

    /**
     * Handle LEFT - cycle to previous action
     */
    ScreenResult onLeft() override {
        int actionCount = 0;
        const ItemAction* actions = getItemActions(selectedIndex_, actionCount);

        if (actions && actionCount > 1) {
            currentActionIndex_--;
            if (currentActionIndex_ < 0) {
                currentActionIndex_ = actionCount - 1;  // Wrap to last
            }

            Serial.print("[ActionableList] Action: ");
            Serial.print(currentActionIndex_);
            Serial.print("/");
            Serial.print(actionCount);
            Serial.print(" - ");
            Serial.println(actions[currentActionIndex_].label);

            updateLCD();  // Update LCD to show new action
        }

        return ScreenResult::stay();
    }

    /**
     * Handle RIGHT - cycle to next action
     */
    ScreenResult onRight() override {
        int actionCount = 0;
        const ItemAction* actions = getItemActions(selectedIndex_, actionCount);

        if (actions && actionCount > 1) {
            currentActionIndex_++;
            if (currentActionIndex_ >= actionCount) {
                currentActionIndex_ = 0;  // Wrap to first
            }

            Serial.print("[ActionableList] Action: ");
            Serial.print(currentActionIndex_);
            Serial.print("/");
            Serial.print(actionCount);
            Serial.print(" - ");
            Serial.println(actions[currentActionIndex_].label);

            updateLCD();  // Update LCD to show new action
        }

        return ScreenResult::stay();
    }

    /**
     * Override update to ensure action index is valid for current item
     * This fixes the bug where navigating to an item with fewer actions
     * leaves currentActionIndex_ out of bounds
     */
    void update() override {
        // Clamp action index if current item has fewer actions
        int actionCount = 0;
        const ItemAction* actions = getItemActions(selectedIndex_, actionCount);

        if (actions && currentActionIndex_ >= actionCount) {
            // Current action index is out of bounds, reset to first action
            currentActionIndex_ = 0;
            updateLCD();  // Update LCD to show the reset action
        }

        ListScreenBase::update();
    }

    // ============================================
    // HELPER METHODS
    // ============================================

    /**
     * Get currently selected action for the selected item
     * @return Current action, or nullptr if no actions
     */
    const ItemAction* getCurrentAction() const {
        int actionCount = 0;
        const ItemAction* actions = getItemActions(selectedIndex_, actionCount);

        if (actions && currentActionIndex_ < actionCount) {
            return &actions[currentActionIndex_];
        }

        return nullptr;
    }

    /**
     * Get current action index
     */
    int getCurrentActionIndex() const {
        return currentActionIndex_;
    }

    /**
     * Reset to first action (call when selection changes)
     */
    void resetAction() {
        currentActionIndex_ = 0;
    }

protected:
    /**
     * Override navigateUp to reset action when changing items
     */
    void navigateUp() {
        int oldIndex = selectedIndex_;
        ListScreenBase::navigateUp();
        if (selectedIndex_ != oldIndex) {
            currentActionIndex_ = 0;
        }
    }

    /**
     * Override navigateDown to reset action when changing items
     */
    void navigateDown() {
        int oldIndex = selectedIndex_;
        ListScreenBase::navigateDown();
        if (selectedIndex_ != oldIndex) {
            currentActionIndex_ = 0;
        }
    }
};

#endif // ACTIONABLE_LIST_SCREEN_BASE_H
