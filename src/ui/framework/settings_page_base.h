#ifndef SETTINGS_PAGE_BASE_H
#define SETTINGS_PAGE_BASE_H

#include "list_screen_base.h"
#include "modal_dialog.h"
#include <Arduino.h>
#include <string.h>  // for memcmp

/**
 * SettingsPageBase - Template for settings screens with automatic save/cancel
 *
 * Features:
 * - Automatic temporary variable management
 * - Save/Cancel buttons added automatically
 * - Dirty state tracking (detects changes)
 * - Confirmation dialog on cancel if dirty
 * - Type-safe settings struct
 *
 * Usage:
 *   struct MIDISettings {
 *       bool drumSampler;
 *       bool crossfeed;
 *       bool reverb;
 *   };
 *
 *   MIDISettings g_midiSettings = { true, true, true };
 *
 *   class MIDISettingsScreen : public SettingsPageBase<MIDISettings> {
 *   public:
 *       MIDISettingsScreen(ScreenContext* ctx)
 *           : SettingsPageBase(ctx, &g_midiSettings, 3) {}
 *
 *       void drawSetting(int index, int row, bool selected) override {
 *           const char* labels[] = {"Drum Sampler", "Crossfeed", "Reverb"};
 *           bool values[] = {temp_.drumSampler, temp_.crossfeed, temp_.reverb};
 *           // ... draw label and value ...
 *       }
 *
 *       void adjustSetting(int index, int delta) override {
 *           bool* settings[] = {&temp_.drumSampler, &temp_.crossfeed, &temp_.reverb};
 *           *settings[index] = !*settings[index];
 *       }
 *   };
 */
template<typename SettingsStruct>
class SettingsPageBase : public ListScreenBase {
protected:
    SettingsStruct temp_;        // Working copy (modified by user)
    SettingsStruct* global_;     // Pointer to global settings
    SettingsStruct original_;    // Original values (for dirty check)
    int numSettings_;            // Number of actual settings (excludes Save/Cancel)
    bool isDirty_;               // Have settings been changed?

public:
    /**
     * Create a settings page
     * @param context - Screen context
     * @param globalSettings - Pointer to global settings struct
     * @param numSettings - Number of settings (excludes Save/Cancel buttons)
     * @param visibleCount - Number of visible items
     */
    SettingsPageBase(ScreenContext* context,
                     SettingsStruct* globalSettings,
                     int numSettings,
                     int visibleCount = 20)
        : ListScreenBase(context, visibleCount, 5, 1),
          global_(globalSettings),
          numSettings_(numSettings),
          isDirty_(false) {}

    virtual ~SettingsPageBase() {}

    // ============================================
    // LIFECYCLE
    // ============================================

    void onEnter() override {
        // Load current values into working copy
        temp_ = *global_;
        original_ = *global_;
        isDirty_ = false;

        Screen::onEnter();
    }

    // ============================================
    // LIST SCREEN OVERRIDES
    // ============================================

    int getItemCount() override {
        return numSettings_ + 2;  // Settings + Save + Cancel
    }

    void drawItem(int itemIndex, int row, bool selected) override {
        if (itemIndex < numSettings_) {
            // Draw setting item
            drawSetting(itemIndex, row, selected);
        } else if (itemIndex == numSettings_) {
            // Save button
            drawSaveButton(row, selected);
        } else {
            // Cancel button
            drawCancelButton(row, selected);
        }
    }

    ScreenResult onItemSelected(int itemIndex) override {
        if (itemIndex == numSettings_) {
            // Save button
            save();
            return ScreenResult::goBack();
        } else if (itemIndex == numSettings_ + 1) {
            // Cancel button
            if (isDirty_) {
                auto result = ModalDialog::showYesNo(
                    context_->ui, context_->lcd,
                    "Discard Changes?",
                    "You have unsaved changes.\nDiscard them?"
                );

                if (result == ModalDialog::RESULT_NO) {
                    return ScreenResult::stay();  // Stay on screen
                }
            }
            cancel();
            return ScreenResult::goBack();
        }

        // For setting items, SELECT does nothing (use LEFT/RIGHT to adjust)
        return ScreenResult::stay();
    }

    ScreenResult onLeft() override {
        if (selectedIndex_ < numSettings_) {
            adjustSetting(selectedIndex_, -1);
            checkDirty();
            draw();
        }
        return ScreenResult::stay();
    }

    ScreenResult onRight() override {
        if (selectedIndex_ < numSettings_) {
            adjustSetting(selectedIndex_, +1);
            checkDirty();
            draw();
        }
        return ScreenResult::stay();
    }

    void updateLCD() override {
        if (!context_->lcdManager) return;

        if (selectedIndex_ < numSettings_) {
            context_->lcdManager->setLine(0, "L/R: Adjust");
            context_->lcdManager->setLine(1, isDirty_ ? "* Modified" : "Sel: Choose");
        } else if (selectedIndex_ == numSettings_) {
            context_->lcdManager->setLine(0, "Sel: Save");
            context_->lcdManager->setLine(1, isDirty_ ? "* Modified" : "No changes");
        } else {
            context_->lcdManager->setLine(0, "Sel: Cancel");
            context_->lcdManager->setLine(1, isDirty_ ? "* Discard?" : "Go back");
        }
    }

protected:
    // ============================================
    // PURE VIRTUAL - Must implement
    // ============================================

    /**
     * Draw an individual setting item
     * @param settingIndex - Index of the setting (0 to numSettings_-1)
     * @param row - Row to draw at
     * @param selected - Is this item selected?
     *
     * Access current values via temp_ member variable
     */
    virtual void drawSetting(int settingIndex, int row, bool selected) = 0;

    /**
     * Adjust a setting value
     * @param settingIndex - Index of the setting
     * @param delta - +1 for increase, -1 for decrease
     *
     * Modify temp_ member variable to change values
     */
    virtual void adjustSetting(int settingIndex, int delta) = 0;

    // ============================================
    // OPTIONAL OVERRIDES
    // ============================================

    /**
     * Called when settings are saved (override for custom logic)
     * e.g., Apply settings to hardware, save to EEPROM, etc.
     */
    virtual void onSave() {}

    /**
     * Called when settings are canceled (override for custom logic)
     */
    virtual void onCancel() {}

    // ============================================
    // SAVE/CANCEL LOGIC
    // ============================================

    /**
     * Save settings to global
     */
    void save() {
        *global_ = temp_;
        original_ = temp_;
        isDirty_ = false;

        onSave();

        // // Serial.print("[SettingsPage] Saved - ");
        // // Serial.println(getSettingsName());
    }

    /**
     * Cancel and revert to original
     */
    void cancel() {
        temp_ = original_;
        isDirty_ = false;

        onCancel();

        // // Serial.print("[SettingsPage] Canceled - ");
        // // Serial.println(getSettingsName());
    }

    /**
     * Check if settings have been modified
     */
    void checkDirty() {
        isDirty_ = memcmp(&temp_, &original_, sizeof(SettingsStruct)) != 0;
    }

    /**
     * Get settings name for logging (optional override)
     */
    virtual const char* getSettingsName() const {
        return "Settings";
    }

private:
    /**
     * Draw the Save button
     */
    void drawSaveButton(int row, bool selected) {
        uint16_t fg = selected ? DOS_BLACK : DOS_GREEN;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;

        context_->ui->fillGridRect(4, row, 72, 1, bg);
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        context_->ui->drawText(6, row, "Save & Exit", fg, bg);

        if (isDirty_) {
            context_->ui->drawText(60, row, "*", DOS_YELLOW, bg);  // Dirty indicator
        }
    }

    /**
     * Draw the Cancel button
     */
    void drawCancelButton(int row, bool selected) {
        uint16_t fg = selected ? DOS_BLACK : DOS_RED;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;

        context_->ui->fillGridRect(4, row, 72, 1, bg);
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        context_->ui->drawText(6, row, "Cancel", fg, bg);
    }
};

#endif // SETTINGS_PAGE_BASE_H
