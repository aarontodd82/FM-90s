#include "screen_factory.h"
#include "screen_context.h"
#include "../screen_new.h"
#include "../screen_id.h"

// Include all migrated screen headers
#include "../main_menu_screen_new.h"
#include "../file_browser_screen_new.h"
#include "../now_playing_screen_new.h"
#include "../settings_screen_new.h"
#include "../bluetooth_settings_screen_new.h"

/**
 * Create a screen with dependency injection
 */
Screen* ScreenFactory::createScreen(ScreenID screenID, ScreenContext* context, void* params) {
    // Validate context
    if (!context) {
        logError(screenID, "Context is null!");
        return nullptr;
    }

    // Validate required dependencies
    if (!context->ui) {
        logError(screenID, "Context->ui is null!");
        return nullptr;
    }

    Screen* screen = nullptr;

    // Create screen based on ID
    switch (screenID) {
        case SCREEN_MAIN_MENU:
            logCreation(screenID, "MainMenuScreenNew");
            screen = new MainMenuScreenNew(context);
            break;

        case SCREEN_FILE_BROWSER_SD:
            logCreation(screenID, "FileBrowserScreenNew (SD)");
            screen = new FileBrowserScreenNew(context, FileBrowserScreenNew::SOURCE_SD);
            break;

        case SCREEN_FILE_BROWSER_USB:
            logCreation(screenID, "FileBrowserScreenNew (USB)");
            screen = new FileBrowserScreenNew(context, FileBrowserScreenNew::SOURCE_USB);
            break;

        case SCREEN_FILE_BROWSER_FLOPPY:
            logCreation(screenID, "FileBrowserScreenNew (Floppy)");
            screen = new FileBrowserScreenNew(context, FileBrowserScreenNew::SOURCE_FLOPPY);
            break;

        case SCREEN_NOW_PLAYING:
            logCreation(screenID, "NowPlayingScreenNew");
            screen = new NowPlayingScreenNew(context);
            break;

        case SCREEN_SETTINGS:
            logCreation(screenID, "SettingsScreenNew");
            screen = new SettingsScreenNew(context);
            break;

        case SCREEN_SETTINGS_MIDI:
            logCreation(screenID, "MIDIAudioSettingsScreenNew");
            screen = new MIDIAudioSettingsScreenNew(context);
            break;

        case SCREEN_SETTINGS_VGM:
            logCreation(screenID, "VGMOptionsScreenNew");
            screen = new VGMOptionsScreenNew(context);
            break;

        case SCREEN_SETTINGS_BLUETOOTH:
            logCreation(screenID, "BluetoothSettingsScreenNew");
            screen = new BluetoothSettingsScreenNew(context);
            break;

        case SCREEN_PLAYLISTS:
            // Not yet migrated - log and return nullptr
            logError(screenID, "SCREEN_PLAYLISTS not yet migrated to new framework");
            return nullptr;

        default:
            logError(screenID, "Unknown screen ID");
            return nullptr;
    }

    // Call onCreate() if screen was created successfully
    if (screen) {
        screen->onCreate(params);
    } else {
        logError(screenID, "Failed to allocate screen (out of memory?)");
    }

    return screen;
}
