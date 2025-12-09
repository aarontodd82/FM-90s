#ifndef SCREEN_NEW_H
#define SCREEN_NEW_H

#include <Arduino.h>
#include "framework/screen_context.h"
#include "framework/screen_result.h"
#include "framework/event_manager.h"  // For EVENT_SCREEN_READY

/**
 * Screen - Base class for all UI screens (NEW FOUNDATION)
 *
 * This is the new foundation for the screen system. It provides:
 * - Complete lifecycle management (onCreate, onEnter, onPause, onResume, onExit, onDestroy)
 * - Dependency injection via ScreenContext
 * - Type-safe navigation via ScreenResult
 * - Optional LCD updates (not forced)
 * - State management support
 *
 * Lifecycle Order:
 * 1. Constructor()
 * 2. onCreate(params)     - Resource allocation, one-time setup
 * 3. onEnter()            - Screen becomes visible
 * 4. [update() loop]      - Screen is active
 * 5. onPause()            - Modal shows, screen goes to background (optional)
 * 6. onResume()           - Modal dismissed, screen returns to foreground (optional)
 * 7. onExit()             - Leaving screen
 * 8. onDestroy()          - Final cleanup
 * 9. Destructor()
 *
 * Usage Example:
 *
 * class MyScreen : public Screen {
 * public:
 *     MyScreen(ScreenContext* ctx) : Screen(ctx) {}
 *
 *     void onCreate(void* params) override {
 *         // Register events, allocate resources
 *         context_->eventManager->on(EVENT_BT_CONNECTED, onBTConnected, this);
 *     }
 *
 *     void onEnter() override {
 *         Screen::onEnter();  // Call base class
 *         // Refresh data, start animations
 *     }
 *
 *     void draw() override {
 *         // Draw your UI
 *     }
 *
 *     ScreenResult onButton(uint8_t button) override {
 *         if (button == BUTTON_SELECT) {
 *             return ScreenResult::navigateTo(SCREEN_SETTINGS);
 *         }
 *         return ScreenResult::stay();
 *     }
 *
 *     void onDestroy() override {
 *         // Unregister events, free resources
 *         context_->eventManager->offAll(this);
 *     }
 * };
 */
class Screen {
protected:
    ScreenContext* context_;   // All dependencies
    bool needsRedraw_;         // Flag to indicate screen needs full redraw

public:
    // ============================================
    // CONSTRUCTION
    // ============================================

    /**
     * Constructor
     * @param context - Dependency injection container (must be valid!)
     */
    Screen(ScreenContext* context)
        : context_(context)
        , needsRedraw_(true)
    {
        if (!context) {
            // // Serial.println("[Screen] ERROR: nullptr context passed to constructor!");
        }
    }

    /**
     * Virtual destructor
     */
    virtual ~Screen() {}

    // ============================================
    // LIFECYCLE HOOKS (override in derived classes)
    // ============================================

    /**
     * Called once when screen is created
     *
     * Use for:
     * - Resource allocation (memory, file handles)
     * - Event registration (EventManager callbacks)
     * - One-time initialization
     *
     * @param params - Optional initialization data from previous screen (can be nullptr)
     *
     * Example:
     *   void onCreate(void* params) override {
     *       // Cast params to expected type
     *       MyData* data = (MyData*)params;
     *       if (data) {
     *           loadFile(data->filename);
     *       }
     *
     *       // Register for events
     *       context_->eventManager->on(EVENT_USB_CONNECTED, onUSB, this);
     *   }
     */
    virtual void onCreate(void* params) {
        // Override in derived class
        (void)params;  // Suppress unused parameter warning
    }

    /**
     * Called when screen becomes visible
     *
     * Use for:
     * - Starting animations
     * - Refreshing data
     * - Initial drawing
     *
     * Note: Called after onCreate() and after onResume()
     */
    virtual void onEnter() {
        Serial.println("[Screen] onEnter: Starting");
        needsRedraw_ = true;

        Serial.println("[Screen] onEnter: Calling draw()");
        draw();

        Serial.println("[Screen] onEnter: Calling updateLCD()");
        updateLCD();

        // Phase 5: Fire EVENT_SCREEN_READY after draw completes
        // PlaybackCoordinator listens for this to know when to start playback
        Serial.println("[Screen] onEnter: Firing EVENT_SCREEN_READY");
        if (context_ && context_->eventManager) {
            context_->eventManager->fire(EventManager::EVENT_SCREEN_READY);
        }

        Serial.println("[Screen] onEnter: Complete");
    }

    /**
     * Called when screen goes to background (modal shows, etc.)
     *
     * Use for:
     * - Pausing animations
     * - Saving state
     * - Stopping timers
     *
     * Example:
     *   void onPause() override {
     *       animation_.pause();
     *       saveState();
     *   }
     */
    virtual void onPause() {
        // Override in derived class
    }

    /**
     * Called when screen returns to foreground
     *
     * Use for:
     * - Resuming animations
     * - Refreshing data
     * - Restoring state
     *
     * Example:
     *   void onResume() override {
     *       animation_.resume();
     *       refreshData();
     *       needsRedraw_ = true;
     *   }
     */
    virtual void onResume() {
        needsRedraw_ = true;
        draw();
        updateLCD();
    }

    /**
     * Called when leaving this screen
     *
     * Use for:
     * - Stopping animations
     * - Saving state
     * - Cleanup before destruction
     */
    virtual void onExit() {
        // Override in derived class
    }

    /**
     * Called when screen is about to be destroyed
     *
     * Use for:
     * - Event unregistration (EventManager callbacks)
     * - Resource deallocation
     * - Final cleanup
     *
     * Example:
     *   void onDestroy() override {
     *       context_->eventManager->offAll(this);
     *       delete myResource_;
     *   }
     */
    virtual void onDestroy() {
        // Override in derived class
    }

    // ============================================
    // RENDERING (override in derived classes)
    // ============================================

    /**
     * Draw the screen on TFT display
     *
     * Called automatically by update() when needsRedraw_ is true
     */
    virtual void draw() = 0;

    /**
     * Update LCD with contextual help (OPTIONAL - override if needed)
     *
     * Default: Do nothing (not all screens need LCD updates)
     *
     * Example:
     *   void updateLCD() override {
     *       context_->lcd->setCursor(0, 0);
     *       context_->lcd->print("Select: Choose");
     *       context_->lcd->setCursor(0, 1);
     *       context_->lcd->print("Back: Cancel");
     *   }
     */
    virtual void updateLCD() {
        // Default: do nothing (not all screens need LCD)
    }

    /**
     * Called periodically for animations/updates
     *
     * Override to add custom update logic
     */
    virtual void update() {
        if (needsRedraw_) {
            draw();
            needsRedraw_ = false;
        }
    }

    // ============================================
    // INPUT HANDLING (override in derived classes)
    // ============================================

    /**
     * Handle button input
     *
     * @param button - Button that was pressed (BUTTON_UP, BUTTON_DOWN, etc.)
     * @return ScreenResult indicating what should happen next
     *
     * Example:
     *   ScreenResult onButton(uint8_t button) override {
     *       if (button == BUTTON_SELECT) {
     *           return ScreenResult::navigateTo(SCREEN_SETTINGS);
     *       } else if (button == BUTTON_UP) {
     *           scrollUp();
     *           return ScreenResult::stay();
     *       }
     *       return ScreenResult::stay();
     *   }
     */
    virtual ScreenResult onButton(uint8_t button) = 0;

    // ============================================
    // UTILITY METHODS
    // ============================================

    /**
     * Request a redraw on next update()
     */
    void requestRedraw() {
        needsRedraw_ = true;
    }

    /**
     * Get screen context (for accessing dependencies)
     *
     * Use context_ member directly for better performance
     * This method is provided for convenience
     */
    ScreenContext* getContext() const {
        return context_;
    }

    /**
     * Check if screen needs redraw
     */
    bool needsRedraw() const {
        return needsRedraw_;
    }
};

#endif // SCREEN_NEW_H
