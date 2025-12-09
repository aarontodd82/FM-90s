#ifndef SCREEN_RESULT_H
#define SCREEN_RESULT_H

#include <Arduino.h>
#include "../screen_id.h"

/**
 * ScreenResult - Type-safe response from screen operations
 *
 * Purpose: Replace int return values with structured, type-safe results
 *
 * Benefits:
 * - Clear intent (STAY vs NAVIGATE vs GO_BACK)
 * - Can pass data between screens
 * - Built-in error handling
 * - Type-safe navigation
 *
 * Old way:
 *   int onButton(uint8_t button) {
 *       return SCREEN_SETTINGS;  // Magic number
 *   }
 *
 * New way:
 *   ScreenResult onButton(uint8_t button) {
 *       return ScreenResult::navigateTo(SCREEN_SETTINGS);
 *   }
 */
struct ScreenResult {
    /**
     * Action to take after screen operation
     */
    enum Action {
        STAY,           // Stay on current screen (no navigation)
        NAVIGATE,       // Navigate to different screen
        GO_BACK,        // Navigate to previous screen
        EXIT_APP,       // Exit application (rare, used for shutdown)
        ERROR           // Error occurred
    };

    Action action;              // What action to take
    ScreenID targetScreen;      // Target screen (if action == NAVIGATE)
    void* data;                 // Optional data to pass to next screen
    const char* errorMsg;       // Error message (if action == ERROR)

    // ============================================
    // FACTORY METHODS (use these to create results)
    // ============================================

    /**
     * Stay on current screen (no navigation)
     */
    static ScreenResult stay() {
        ScreenResult result;
        result.action = STAY;
        result.targetScreen = (ScreenID)-1;
        result.data = nullptr;
        result.errorMsg = nullptr;
        return result;
    }

    /**
     * Navigate to a specific screen
     * @param screen - Target screen ID
     * @param params - Optional data to pass to next screen
     */
    static ScreenResult navigateTo(ScreenID screen, void* params = nullptr) {
        ScreenResult result;
        result.action = NAVIGATE;
        result.targetScreen = screen;
        result.data = params;
        result.errorMsg = nullptr;
        return result;
    }

    /**
     * Go back to previous screen
     */
    static ScreenResult goBack() {
        ScreenResult result;
        result.action = GO_BACK;
        result.targetScreen = (ScreenID)-1;
        result.data = nullptr;
        result.errorMsg = nullptr;
        return result;
    }

    /**
     * Exit the application
     */
    static ScreenResult exitApp() {
        ScreenResult result;
        result.action = EXIT_APP;
        result.targetScreen = (ScreenID)-1;
        result.data = nullptr;
        result.errorMsg = nullptr;
        return result;
    }

    /**
     * Signal an error occurred
     * @param msg - Error message to display/log
     */
    static ScreenResult error(const char* msg) {
        ScreenResult result;
        result.action = ERROR;
        result.targetScreen = (ScreenID)-1;
        result.data = nullptr;
        result.errorMsg = msg;
        return result;
    }

    // ============================================
    // QUERY METHODS
    // ============================================

    bool isStay() const { return action == STAY; }
    bool isNavigate() const { return action == NAVIGATE; }
    bool isGoBack() const { return action == GO_BACK; }
    bool isExitApp() const { return action == EXIT_APP; }
    bool isError() const { return action == ERROR; }

    /**
     * Check if this result requests a screen change
     */
    bool requestsNavigation() const {
        return action == NAVIGATE || action == GO_BACK || action == EXIT_APP;
    }
};

#endif // SCREEN_RESULT_H
