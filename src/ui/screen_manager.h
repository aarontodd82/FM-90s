#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <Arduino.h>
#include "screen_new.h"
#include "screen_id.h"
#include "framework/screen_context.h"
#include "framework/screen_result.h"
#include "framework/screen_factory.h"
#include "framework/status_bar_manager.h"
#include "../retro_ui.h"
#include <Adafruit_RGBLCDShield.h>

/**
 * ScreenManager - Navigation controller and button handler
 *
 * Responsibilities:
 * - Screen lifecycle management (create, enter, exit, destroy)
 * - Button input handling with debouncing and auto-repeat
 * - Back navigation history
 * - Screen update coordination
 *
 * Uses new framework:
 * - ScreenContext for dependency injection
 * - ScreenFactory for screen creation
 * - ScreenResult for type-safe navigation
 */

// Button configuration
#define DEBOUNCE_DELAY 50     // ms to wait for debounce
#define REPEAT_DELAY   400    // ms before key repeat starts (hold time)
#define REPEAT_RATE    80     // ms between repeats (fast scrolling)

class ScreenManager {
private:
    static ScreenManager* instance;

    Screen* currentScreen_;
    ScreenID currentScreenID_;
    ScreenID previousScreenID_;
    ScreenContext* context_;

    // Deferred navigation (prevents use-after-free when navigating during event handlers)
    bool hasPendingNavigation_;
    ScreenID pendingScreenID_;
    void* pendingParams_;

    // Button handling
    uint8_t lastButtons_;

    // Auto-repeat state
    uint8_t repeatButton_;
    uint32_t buttonPressTime_;
    uint32_t lastRepeatTime_;
    bool repeatActive_;

    // Button read throttling (prevent I2C blocking audio)
    uint32_t lastButtonReadTime_;
    static const uint32_t BUTTON_READ_INTERVAL = 50;

    // Private constructor for singleton
    ScreenManager()
        : currentScreen_(nullptr)
        , currentScreenID_(SCREEN_NONE)
        , previousScreenID_(SCREEN_NONE)
        , context_(nullptr)
        , hasPendingNavigation_(false)
        , pendingScreenID_(SCREEN_NONE)
        , pendingParams_(nullptr)
        , lastButtons_(0)
        , repeatButton_(0)
        , buttonPressTime_(0)
        , lastRepeatTime_(0)
        , repeatActive_(false)
        , lastButtonReadTime_(0)
    {}

    /**
     * Delete the current screen and clean up
     */
    void deleteCurrentScreen() {
        if (currentScreen_) {
            currentScreen_->onExit();
            currentScreen_->onDestroy();
            delete currentScreen_;
            currentScreen_ = nullptr;
        }
    }

public:
    /**
     * Get singleton instance
     */
    static ScreenManager* getInstance() {
        if (!instance) {
            instance = new ScreenManager();
        }
        return instance;
    }

    /**
     * Initialize with screen context (new framework pattern)
     * @param context - Populated ScreenContext with all dependencies
     */
    void init(ScreenContext* context) {
        if (!context) {
            // // Serial.println("[ScreenManager] ERROR: Context is null!");
            return;
        }

        if (!context->isValid()) {
            // // Serial.println("[ScreenManager] ERROR: Context validation failed!");
            return;
        }

        context_ = context;
        // // Serial.println("[ScreenManager] Initialized with ScreenContext");
    }

    /**
     * Get current screen ID
     */
    ScreenID getCurrentScreenID() const {
        return currentScreenID_;
    }

    /**
     * Get previous screen ID (for back navigation)
     */
    ScreenID getPreviousScreenID() const {
        return previousScreenID_;
    }

    /**
     * Request deferred navigation (safe to call during event handlers)
     * Navigation will happen at the start of next update() cycle
     * @param screenID - Target screen ID
     * @param params - Optional parameters to pass to onCreate()
     */
    void requestNavigation(ScreenID screenID, void* params = nullptr) {
        Serial.printf("[ScreenManager] requestNavigation: screen=%d (deferred)\n", (int)screenID);
        hasPendingNavigation_ = true;
        pendingScreenID_ = screenID;
        pendingParams_ = params;
    }

    /**
     * Switch to a specific screen (IMMEDIATE - use requestNavigation() if called from event handler!)
     * @param screenID - Target screen
     * @param params - Optional parameters to pass to onCreate()
     */
    void switchTo(ScreenID screenID, void* params = nullptr) {
        if (screenID == SCREEN_NONE || screenID == currentScreenID_) {
            return;
        }

        // Handle special "go back" request
        if (screenID == SCREEN_GO_BACK) {
            if (previousScreenID_ != SCREEN_NONE && previousScreenID_ != SCREEN_NOW_PLAYING) {
                screenID = previousScreenID_;
                // // Serial.print("[ScreenManager] Going back to previous screen: ");
                // // Serial.println((int)screenID);
            } else {
                screenID = SCREEN_MAIN_MENU;
                // // Serial.println("[ScreenManager] No valid previous screen, going to main menu");
            }
        } else {
            // // Serial.print("[ScreenManager] Switching to screen: ");
            // // Serial.println((int)screenID);
        }

        // Track previous screen for back navigation
        previousScreenID_ = currentScreenID_;

        // Clean up old screen
        deleteCurrentScreen();

        // Create new screen using ScreenFactory (new framework pattern)
        currentScreenID_ = screenID;
        currentScreen_ = ScreenFactory::createScreen(screenID, context_, params);

        // Notify StatusBarManager of screen change (so it can adjust content)
        if (context_ && context_->statusBarManager) {
            context_->statusBarManager->setCurrentScreen(screenID);
        }

        // // Serial.println(">>> ScreenManager - screen created, checking...");
        Serial.flush();

        if (currentScreen_) {
            Serial.println("[ScreenManager] switchTo: Calling onEnter()");
            Serial.flush();
            currentScreen_->onEnter();
            Serial.println("[ScreenManager] switchTo: onEnter() returned, exiting switchTo()");
            Serial.flush();
        } else {
            Serial.print("[ScreenManager] ERROR: Failed to create screen ");
            Serial.println((int)screenID);
        }

        Serial.println("[ScreenManager] switchTo: Complete");
        Serial.flush();
    }

    /**
     * Process button input with edge detection and auto-repeat
     */
    void update() {
        if (!context_ || !context_->lcd) return;

        // CRITICAL: Handle pending navigation FIRST (before accessing currentScreen_)
        // This prevents use-after-free when event handlers request navigation
        if (hasPendingNavigation_) {
            Serial.println("[ScreenManager] update: Processing pending navigation");
            ScreenID targetScreen = pendingScreenID_;
            void* params = pendingParams_;
            hasPendingNavigation_ = false;
            pendingScreenID_ = SCREEN_NONE;
            pendingParams_ = nullptr;
            switchTo(targetScreen, params);
        }

        if (!currentScreen_) return;

        uint32_t now = millis();

        // Update RetroUI status notifications (auto-hide after timeout)
        if (context_->ui) {
            context_->ui->updateStatusNotification();
        }

        // Update current screen
        currentScreen_->update();

        // Throttle button reads to prevent I2C blocking audio
        if (now - lastButtonReadTime_ < BUTTON_READ_INTERVAL) {
            return;
        }

        // Read buttons (I2C communication)
        lastButtonReadTime_ = now;
        uint8_t buttons = context_->lcd->readButtons();

        // Detect rising edge (button press, not release)
        uint8_t pressed = buttons & ~lastButtons_;

        if (pressed) {
            // New button press - handle immediately
            if (pressed & BUTTON_UP) {
                processButton(BUTTON_UP);
                repeatButton_ = BUTTON_UP;
            } else if (pressed & BUTTON_DOWN) {
                processButton(BUTTON_DOWN);
                repeatButton_ = BUTTON_DOWN;
            } else if (pressed & BUTTON_LEFT) {
                processButton(BUTTON_LEFT);
                repeatButton_ = BUTTON_LEFT;
            } else if (pressed & BUTTON_RIGHT) {
                processButton(BUTTON_RIGHT);
                repeatButton_ = BUTTON_RIGHT;
            } else if (pressed & BUTTON_SELECT) {
                processButton(BUTTON_SELECT);
                repeatButton_ = 0;  // SELECT doesn't repeat
            }

            // Start auto-repeat timer
            buttonPressTime_ = now;
            lastRepeatTime_ = now;
            repeatActive_ = false;
        }
        // Button still held down
        else if (buttons != 0 && buttons == lastButtons_) {
            // Only auto-repeat UP/DOWN (navigation buttons)
            if ((buttons & BUTTON_UP) || (buttons & BUTTON_DOWN)) {
                uint32_t holdTime = now - buttonPressTime_;

                // Start repeating after REPEAT_DELAY
                if (!repeatActive_ && holdTime >= REPEAT_DELAY) {
                    repeatActive_ = true;
                    lastRepeatTime_ = now;
                }

                // Send repeat events at REPEAT_RATE
                if (repeatActive_ && (now - lastRepeatTime_) >= REPEAT_RATE) {
                    if (buttons & BUTTON_UP) {
                        processButton(BUTTON_UP);
                    } else if (buttons & BUTTON_DOWN) {
                        processButton(BUTTON_DOWN);
                    }
                    lastRepeatTime_ = now;
                }
            }
        }
        // Button released
        else if (buttons == 0 && lastButtons_ != 0) {
            // Reset auto-repeat state
            repeatButton_ = 0;
            repeatActive_ = false;
        }

        // Update button state for next iteration
        lastButtons_ = buttons;
    }

    /**
     * Get pointer to current screen (for external updates)
     */
    Screen* getCurrentScreen() {
        return currentScreen_;
    }

    /**
     * Force a redraw of the current screen
     */
    void requestRedraw() {
        if (currentScreen_) {
            currentScreen_->requestRedraw();
        }
    }

private:
    /**
     * Process a button press (new framework pattern using ScreenResult)
     */
    void processButton(uint8_t button) {
        if (!currentScreen_) return;

        // Let the screen handle the button (returns ScreenResult)
        ScreenResult result = currentScreen_->onButton(button);

        // Handle navigation requests
        if (result.requestsNavigation()) {
            if (result.isGoBack()) {
                switchTo(SCREEN_GO_BACK);
            } else if (result.isNavigate()) {
                switchTo(result.targetScreen, result.data);
            } else if (result.isExitApp()) {
                // // Serial.println("[ScreenManager] Exit app requested");
                // Could handle app exit here if needed
            }
        } else if (result.isError()) {
            // // Serial.print("[ScreenManager] Screen error: ");
            // // Serial.println(result.errorMsg);
        }
    }
};

// Initialize static member
inline ScreenManager* ScreenManager::instance = nullptr;

#endif // SCREEN_MANAGER_H
