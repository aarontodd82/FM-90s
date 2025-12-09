#ifndef SETTINGS_SCREEN_NEW_H
#define SETTINGS_SCREEN_NEW_H

#include "framework/list_screen_base.h"
#include "framework/settings_page_base.h"
#include "framework/modal_dialog.h"
#include "framework/status_bar_manager.h"
#include "screen_id.h"
#include "lcd_symbols.h"
#include "../dos_colors.h"

// External global settings from main.cpp
extern bool g_drumSamplerEnabled;
extern bool g_crossfeedEnabled;
extern bool g_reverbEnabled;
extern uint8_t g_maxLoopsBeforeFade;
extern float g_fadeDurationSeconds;

/**
 * SettingsScreenNew - Main settings menu using new framework
 *
 * Simple category list using ListScreenBase
 */
class SettingsScreenNew : public ListScreenBase {
private:
    struct CategoryItem {
        const char* label;
        const char* description;
        const char* icon;
        ScreenID targetScreen;
    };

    static const int CATEGORY_ITEMS = 4;
    CategoryItem categories_[CATEGORY_ITEMS];

public:
    SettingsScreenNew(ScreenContext* context)
        : ListScreenBase(context, 5, 5, 3) {  // 5 visible, row 5, spacing 3

        // Initialize categories (can't use initializer list with structs containing non-const members)
        categories_[0] = {" MIDI Audio",       "MIDI playback",   "\x0E", SCREEN_SETTINGS_MIDI};
        categories_[1] = {" VGM Options",      "Video game music","\x0F", SCREEN_SETTINGS_VGM};
        categories_[2] = {" Bluetooth Audio",  "BT connection",   "\x02", SCREEN_SETTINGS_BLUETOOTH};
        categories_[3] = {" Back to Main Menu","Exit settings",   "\x1B", SCREEN_MAIN_MENU};
    }

    // ============================================
    // LIST SCREEN BASE IMPLEMENTATION
    // ============================================

    int getItemCount() override {
        return CATEGORY_ITEMS;
    }

    void drawItem(int itemIndex, int row, bool selected) override {
        if (itemIndex < 0 || itemIndex >= CATEGORY_ITEMS) return;

        const CategoryItem& item = categories_[itemIndex];

        // Build display text with icon
        char itemText[100];
        snprintf(itemText, sizeof(itemText), "%s%s", item.icon, item.label);

        // DOS-style colors
        uint16_t fg = selected ? DOS_BLACK : DOS_WHITE;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;

        // Fill row background
        context_->ui->fillGridRect(4, row, 72, 1, bg);

        // Draw selection arrow if selected
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        // Draw category name
        context_->ui->drawText(6, row, itemText, fg, bg);
    }

    ScreenResult onItemSelected(int itemIndex) override {
        if (itemIndex < 0 || itemIndex >= CATEGORY_ITEMS) {
            return ScreenResult::stay();
        }

        // Navigate to target screen
        return ScreenResult::navigateTo(categories_[itemIndex].targetScreen);
    }

    // ============================================
    // DISPLAY METHODS
    // ============================================

    void drawHeader() override {
        // DOS-style header (drawWindow fills background automatically)
        context_->ui->drawWindow(0, 0, 100, 30, " SETTINGS ", DOS_WHITE, DOS_BLUE);
    }

    void drawFooter() override {
        // Horizontal separator (DOS style)
        context_->ui->drawHLine(0, 28, 100, DOS_WHITE);

        // Global status bar (shows "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->draw();
        }
    }

    void update() override {
        // Update global status bar (dynamic "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->update();
        }

        // Call base class update
        ListScreenBase::update();
    }

    void updateLCD() override {
        if (!context_->lcdManager) return;

        context_->lcdManager->setLine(0, "< SETTINGS >");

        if (selectedIndex_ >= 0 && selectedIndex_ < CATEGORY_ITEMS) {
            context_->lcdManager->setLine(1, categories_[selectedIndex_].description);
        } else {
            context_->lcdManager->clearLine(1);
        }
    }
};

// ============================================
// MIDI AUDIO SETTINGS - Using SettingsPageBase
// ============================================

struct MIDIAudioSettings {
    bool drumSamplerEnabled;
    bool crossfeedEnabled;
    bool reverbEnabled;
};

// Global settings instance
MIDIAudioSettings g_midiAudioSettings = {
    true,  // drumSamplerEnabled
    true,  // crossfeedEnabled
    true   // reverbEnabled
};

class MIDIAudioSettingsScreenNew : public SettingsPageBase<MIDIAudioSettings> {
private:
    static const char* settingLabels_[3];

public:
    MIDIAudioSettingsScreenNew(ScreenContext* context)
        : SettingsPageBase(context, &g_midiAudioSettings, 3, 5) {}  // 3 settings, 5 visible items

    // ============================================
    // SETTINGS PAGE BASE IMPLEMENTATION
    // ============================================

    void drawSetting(int settingIndex, int row, bool selected) override {
        if (settingIndex < 0 || settingIndex >= 3) return;

        const char* label = settingLabels_[settingIndex];
        bool value;

        switch (settingIndex) {
            case 0: value = temp_.drumSamplerEnabled; break;
            case 1: value = temp_.crossfeedEnabled; break;
            case 2: value = temp_.reverbEnabled; break;
            default: return;
        }

        // DOS-style colors
        uint16_t fg = selected ? DOS_BLACK : DOS_WHITE;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;
        uint16_t valueFg = selected ? DOS_BLACK : DOS_YELLOW;

        // Fill row background
        context_->ui->fillGridRect(4, row, 72, 1, bg);

        // Draw selection arrow if selected
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        // Draw label
        context_->ui->drawText(6, row, label, fg, bg);

        // Draw value
        const char* valueStr = value ? "ON" : "OFF";
        context_->ui->drawText(60, row, valueStr, valueFg, bg);
    }

    void adjustSetting(int settingIndex, int delta) override {
        // Boolean toggle - delta doesn't matter
        (void)delta;

        switch (settingIndex) {
            case 0: temp_.drumSamplerEnabled = !temp_.drumSamplerEnabled; break;
            case 1: temp_.crossfeedEnabled = !temp_.crossfeedEnabled; break;
            case 2: temp_.reverbEnabled = !temp_.reverbEnabled; break;
        }
    }

    // ============================================
    // DISPLAY METHODS
    // ============================================

    void drawHeader() override {
        context_->ui->drawWindow(0, 0, 100, 30, " MIDI AUDIO ", DOS_WHITE, DOS_BLUE);
    }

    const char* getSettingsName() const override {
        return "MIDI Audio";
    }

    // ============================================
    // LIFECYCLE
    // ============================================

    void onSave() override {
        // Apply settings to globals
        g_drumSamplerEnabled = temp_.drumSamplerEnabled;
        g_crossfeedEnabled = temp_.crossfeedEnabled;
        g_reverbEnabled = temp_.reverbEnabled;

        // // Serial.println("[MIDIAudioSettings] Settings saved and applied!");

        // Fire event so audio system can update dynamically
        if (context_->eventManager) {
            context_->eventManager->fire(EventManager::EVENT_AUDIO_SETTINGS_CHANGED);
        }
    }
};

// Static member definitions
const char* MIDIAudioSettingsScreenNew::settingLabels_[3] = {
    "PCM Drum Sampler",
    "Stereo Crossfeed",
    "Reverb Effect"
};

// ============================================
// VGM OPTIONS SETTINGS - Using SettingsPageBase
// ============================================

struct VGMOptionsSettings {
    uint8_t maxLoopsBeforeFade;   // 0 = Forever, 1-5 = number of plays
    float fadeDurationSeconds;    // 5, 7, 10, 15, 20
    bool nesFiltersEnabled;       // NES APU output filters
    bool nesStereoEnabled;        // NES APU stereo panning
    bool spcFilterEnabled;        // SPC gaussian filter (for authentic SNES sound)
};

// Global settings instance
VGMOptionsSettings g_vgmOptionsSettings = {
    2,     // maxLoopsBeforeFade (2 plays default)
    7.0f,  // fadeDurationSeconds (7 seconds default)
    false, // nesFiltersEnabled (OFF by default for raw sound)
    true,  // nesStereoEnabled (ON by default)
    false  // spcFilterEnabled (OFF by default for raw sound)
};

class VGMOptionsScreenNew : public SettingsPageBase<VGMOptionsSettings> {
private:
    static const char* settingLabels_[5];  // Now 5 settings

public:
    VGMOptionsScreenNew(ScreenContext* context)
        : SettingsPageBase(context, &g_vgmOptionsSettings, 5, 5) {}  // 5 settings, 5 visible items

    // ============================================
    // SETTINGS PAGE BASE IMPLEMENTATION
    // ============================================

    void drawSetting(int settingIndex, int row, bool selected) override {
        if (settingIndex < 0 || settingIndex >= 5) return;  // Now 5 settings

        const char* label = settingLabels_[settingIndex];
        char valueStr[16];

        // Get value string
        switch (settingIndex) {
            case 0:  // Max Loops Before Fade
                if (temp_.maxLoopsBeforeFade == 0) {
                    snprintf(valueStr, sizeof(valueStr), "Forever");
                } else {
                    snprintf(valueStr, sizeof(valueStr), "%d play%s",
                             temp_.maxLoopsBeforeFade,
                             temp_.maxLoopsBeforeFade > 1 ? "s" : "");
                }
                break;
            case 1:  // Fade Duration
                snprintf(valueStr, sizeof(valueStr), "%ds", (int)temp_.fadeDurationSeconds);
                break;
            case 2:  // NES Filters
                snprintf(valueStr, sizeof(valueStr), "%s", temp_.nesFiltersEnabled ? "ON" : "OFF");
                break;
            case 3:  // NES Stereo
                snprintf(valueStr, sizeof(valueStr), "%s", temp_.nesStereoEnabled ? "ON" : "OFF");
                break;
            case 4:  // SPC Filter
                snprintf(valueStr, sizeof(valueStr), "%s", temp_.spcFilterEnabled ? "ON" : "OFF");
                break;
            default:
                return;
        }

        // DOS-style colors
        uint16_t fg = selected ? DOS_BLACK : DOS_WHITE;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;
        uint16_t valueFg = selected ? DOS_BLACK : DOS_YELLOW;

        // Fill row background
        context_->ui->fillGridRect(4, row, 72, 1, bg);

        // Draw selection arrow if selected
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        // Draw label
        context_->ui->drawText(6, row, label, fg, bg);

        // Draw value
        context_->ui->drawText(55, row, valueStr, valueFg, bg);
    }

    void adjustSetting(int settingIndex, int delta) override {
        switch (settingIndex) {
            case 0:  // Max Loops Before Fade (0=Forever, 1-5)
                if (delta > 0) {
                    if (temp_.maxLoopsBeforeFade < 5) {
                        temp_.maxLoopsBeforeFade++;
                    }
                } else {
                    if (temp_.maxLoopsBeforeFade > 0) {
                        temp_.maxLoopsBeforeFade--;
                    }
                }
                break;

            case 1:  // Fade Duration (5, 7, 10, 15, 20 seconds)
                if (delta > 0) {
                    // Increase
                    if (temp_.fadeDurationSeconds < 20.0f) {
                        if (temp_.fadeDurationSeconds == 5.0f) temp_.fadeDurationSeconds = 7.0f;
                        else if (temp_.fadeDurationSeconds == 7.0f) temp_.fadeDurationSeconds = 10.0f;
                        else if (temp_.fadeDurationSeconds == 10.0f) temp_.fadeDurationSeconds = 15.0f;
                        else if (temp_.fadeDurationSeconds == 15.0f) temp_.fadeDurationSeconds = 20.0f;
                    }
                } else {
                    // Decrease
                    if (temp_.fadeDurationSeconds > 5.0f) {
                        if (temp_.fadeDurationSeconds == 7.0f) temp_.fadeDurationSeconds = 5.0f;
                        else if (temp_.fadeDurationSeconds == 10.0f) temp_.fadeDurationSeconds = 7.0f;
                        else if (temp_.fadeDurationSeconds == 15.0f) temp_.fadeDurationSeconds = 10.0f;
                        else if (temp_.fadeDurationSeconds == 20.0f) temp_.fadeDurationSeconds = 15.0f;
                    }
                }
                break;

            case 2:  // NES Filters (ON/OFF toggle)
                temp_.nesFiltersEnabled = !temp_.nesFiltersEnabled;
                break;

            case 3:  // NES Stereo (ON/OFF toggle)
                temp_.nesStereoEnabled = !temp_.nesStereoEnabled;
                break;

            case 4:  // SPC Filter (ON/OFF toggle)
                temp_.spcFilterEnabled = !temp_.spcFilterEnabled;
                break;
        }
    }

    // ============================================
    // DISPLAY METHODS
    // ============================================

    void drawHeader() override {
        context_->ui->drawWindow(0, 0, 100, 30, " VGM OPTIONS ", DOS_WHITE, DOS_BLUE);
    }

    const char* getSettingsName() const override {
        return "VGM Options";
    }

    // ============================================
    // LIFECYCLE
    // ============================================

    void onSave() override {
        // Apply settings to globals
        extern uint8_t g_maxLoopsBeforeFade;
        extern float g_fadeDurationSeconds;
        extern bool g_nesFiltersEnabled;
        extern bool g_nesStereoEnabled;
        extern bool g_spcFilterEnabled;

        g_maxLoopsBeforeFade = temp_.maxLoopsBeforeFade;
        g_fadeDurationSeconds = temp_.fadeDurationSeconds;
        g_nesFiltersEnabled = temp_.nesFiltersEnabled;
        g_nesStereoEnabled = temp_.nesStereoEnabled;
        g_spcFilterEnabled = temp_.spcFilterEnabled;

        // // Serial.println("[VGMOptions] Settings saved and applied!");
    }
};

// Static member definitions
const char* VGMOptionsScreenNew::settingLabels_[5] = {
    "Looping: Fade After",
    "Fade Duration",
    "NES Filters",
    "NES Stereo",
    "SPC Filter"
};

#endif // SETTINGS_SCREEN_NEW_H
