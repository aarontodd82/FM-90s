#ifndef SCREEN_FACTORY_H
#define SCREEN_FACTORY_H

#include "../screen_new.h"
#include "../screen_id.h"
#include "screen_context.h"
#include "screen_result.h"

// Forward declare all screen classes (don't include headers to avoid circular deps)
class MainMenuScreenV2;
class FileBrowserScreenV2;
class NowPlayingScreen;
class SettingsScreenV2;
class MIDIAudioSettingsScreen;
class VGMLoopingSettingsScreen;
class BluetoothSettingsScreen;

/**
 * ScreenFactory - Creates screens with proper dependency injection
 *
 * Purpose: Separate screen creation logic from ScreenManager
 *
 * Benefits:
 * - Single responsibility (only creates screens)
 * - Centralized dependency injection
 * - Automatic onCreate() calling
 * - Easy to add new screens
 * - Validation logic in one place
 *
 * Usage:
 *   Screen* screen = ScreenFactory::createScreen(SCREEN_SETTINGS, context, params);
 *   if (!screen) {
 *       // Handle error
 *   }
 */
class ScreenFactory {
public:
    /**
     * Create a screen with dependency injection
     *
     * @param screenID - Which screen to create
     * @param context - Dependency injection container (required!)
     * @param params - Optional initialization data to pass to onCreate()
     * @return New screen instance, or nullptr on error
     *
     * Note: Caller is responsible for deleting the screen!
     */
    static Screen* createScreen(ScreenID screenID, ScreenContext* context, void* params = nullptr);

private:
    /**
     * Log screen creation
     */
    static void logCreation(ScreenID screenID, const char* screenName) {
        // // Serial.print("[ScreenFactory] Creating screen: ");
        // // Serial.print(screenName);
        // // Serial.print(" (ID=");
        // // Serial.print((int)screenID);
        // // Serial.println(")");
    }

    /**
     * Log screen creation failure
     */
    static void logError(ScreenID screenID, const char* reason) {
        // // Serial.print("[ScreenFactory] ERROR: Failed to create screen ID ");
        // // Serial.print((int)screenID);
        // // Serial.print(" - ");
        // // Serial.println(reason);
    }
};

#endif // SCREEN_FACTORY_H
