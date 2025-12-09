#ifndef SCREEN_ID_H
#define SCREEN_ID_H

/**
 * Screen IDs - Enum for all screens in the application
 *
 * Separated from screen_manager.h to avoid circular dependencies
 * when screens need to reference other screen IDs.
 */
enum ScreenID {
    SCREEN_NONE = -1,
    SCREEN_GO_BACK = -2,  // Special ID to return to previous screen
    SCREEN_MAIN_MENU = 100,
    SCREEN_FILE_BROWSER_SD = 101,
    SCREEN_FILE_BROWSER_USB = 102,
    SCREEN_FILE_BROWSER_FLOPPY = 103,
    SCREEN_PLAYLISTS = 104,
    SCREEN_NOW_PLAYING = 105,
    SCREEN_SETTINGS = 106,
    SCREEN_SETTINGS_MIDI = 107,      // MIDI Audio settings sub-screen
    SCREEN_SETTINGS_VGM = 108,       // VGM Looping settings sub-screen
    SCREEN_SETTINGS_BLUETOOTH = 109  // Bluetooth settings sub-screen
};

#endif // SCREEN_ID_H
