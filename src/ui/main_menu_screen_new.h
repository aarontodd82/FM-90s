#ifndef MAIN_MENU_SCREEN_NEW_H
#define MAIN_MENU_SCREEN_NEW_H

#include "framework/actionable_list_screen_base.h"
#include "framework/event_manager.h"
#include "framework/playback_navigation_handler.h"
#include "framework/status_bar_manager.h"
#include "screen_id.h"
#include "lcd_symbols.h"
#include "../dos_colors.h"
#include "../usb_drive_manager.h"
#include "../floppy_manager.h"
#include "../playback_state.h"
#include "../playback_coordinator.h"  // For requestStop/requestPlay
#include "../queue_manager.h"  // For queue operations

/**
 * MainMenuScreenNew - Main menu using ActionableListScreenBase
 *
 * Features:
 * - Multi-action support for "Current Queue" item
 * - Dynamic menu (5 or 6 items depending on queue state)
 * - Context-aware queue actions (Play/Stop/Clear based on playback state)
 * - Event-driven updates (playback/queue changes)
 * - ScreenContext dependency injection
 * - ScreenResult type-safe navigation
 * - Preserves original DOS styling
 */
class MainMenuScreenNew : public ActionableListScreenBase {
private:
    struct MenuItem {
        const char* label;
        const char* icon;       // Icon character (DOS extended ASCII)
        char status[32];        // Status text shown on right (now mutable)
        ScreenID targetScreen;  // Where SELECT goes
        const char* lcdHelp;    // LCD line 1 text
    };

    static const int MAX_MENU_ITEMS = 6;  // 5 standard + 1 dynamic (Current Queue)
    MenuItem menuItems_[MAX_MENU_ITEMS];
    bool showQueueItem_;  // Dynamic: show "Current Queue" item?

    // Queue actions (context-aware)
    static ItemAction queueActions_[2];  // Actions change based on state

public:
    MainMenuScreenNew(ScreenContext* context)
        : ActionableListScreenBase(context, 20, 5, 3),  // 20 visible (more than needed), row 5, spacing 3
          showQueueItem_(false) {

        // Initialize menu items (can't use initializer list due to mutable status)
        menuItems_[0] = {" Playlists",     "\x03", "[24 playlists]", SCREEN_PLAYLISTS,          "Browse playlists"};
        menuItems_[1] = {" SD Card",       "\xFE", "[Ready]",        SCREEN_FILE_BROWSER_SD,    "SD Card files"};
        menuItems_[2] = {" USB Drive",     " ",    "",               SCREEN_FILE_BROWSER_USB,   "USB flash drive"};
        menuItems_[3] = {" Floppy Drive",  " ",    "[Ready]",        SCREEN_FILE_BROWSER_FLOPPY,"Floppy ready"};
        menuItems_[4] = {" Settings",      "\x0F", "",               SCREEN_SETTINGS,            "Configure player"};
        // menuItems_[5] is for dynamic "Current Queue" item (initialized in updateQueueItem)
    }

    // ============================================
    // LIFECYCLE
    // ============================================

    void onCreate(void* params) override {
        // Listen for playback and queue events to show/hide queue item
        if (context_ && context_->eventManager) {
            context_->eventManager->on(EventManager::EVENT_PLAYBACK_STARTED, onPlaybackStarted, this);
            context_->eventManager->onInt(EventManager::EVENT_PLAYBACK_STOPPED_COMPLETE, onPlaybackStopped, this);
            context_->eventManager->on(EventManager::EVENT_QUEUE_CHANGED, onQueueEvent, this);
        }
        Serial.println("[MainMenu] Created with event listeners");
    }

    void onEnter() override {
        // Call base class first (will call draw() and updateLCD())
        ActionableListScreenBase::onEnter();

        // Update dynamic content AFTER initial draw to avoid double-draw
        updateFloppyStatus();
        updateQueueItemState();
    }

    void onDestroy() override {
        // Unregister all events
        if (context_->eventManager) {
            context_->eventManager->offAll(this);
        }
    }

    void update() override {
        // Update global status bar (dynamic "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->update();
        }

        // Call base class update
        ActionableListScreenBase::update();
    }

    // ============================================
    // LIST SCREEN BASE IMPLEMENTATION
    // ============================================

    int getItemCount() override {
        // Dynamic count: 5 standard items + 1 optional queue item
        return showQueueItem_ ? 6 : 5;
    }

    const ItemAction* getItemActions(int itemIndex, int& count) override {
        // Only queue item (index 5 when shown) has actions
        if (showQueueItem_ && itemIndex == 5) {
            count = 2;  // Always 2 actions: Primary action + Clear
            return queueActions_;
        }

        // All other items: no actions (simple SELECT navigation)
        count = 0;
        return nullptr;
    }

    ScreenResult onActionExecuted(int itemIndex, int actionIndex) override {
        // Only queue item has actions
        if (!showQueueItem_ || itemIndex != 5) {
            return ScreenResult::stay();
        }

        if (actionIndex >= 2) {
            return ScreenResult::stay();
        }

        PlaybackState* state = PlaybackState::getInstance();
        bool isPlaying = (state->getStatus() == PLAYBACK_PLAYING);

        if (isPlaying) {
            // Playing state
            if (actionIndex == 0) {
                // Action 0: "Go to Now Playing"
                Serial.println("[MainMenu] Queue action: Go to Now Playing");
                return ScreenResult::navigateTo(SCREEN_NOW_PLAYING);
            } else {
                // Action 1: "Stop & Clear"
                Serial.println("[MainMenu] Queue action: Stop & Clear");
                if (context_->coordinator) {
                    context_->coordinator->requestStop(StopReason::USER_REQUEST);
                }
                if (context_->queueManager) {
                    context_->queueManager->clear();
                }
                return ScreenResult::stay();
            }
        } else {
            // Stopped state (but has queue)
            if (actionIndex == 0) {
                // Action 0: "Start playback"
                Serial.println("[MainMenu] Queue action: Start playback");
                if (context_->queueManager && !context_->queueManager->isEmpty()) {
                    // Notify handler that user wants to see Now Playing screen
                    PlaybackNavigationHandler::notifyUserWantsNowPlaying();

                    // Use playNext(nullptr) to dequeue first track
                    // This removes it from queue so auto-advance works correctly
                    const char* firstTrack = context_->queueManager->playNext(nullptr);
                    if (firstTrack && context_->coordinator) {
                        context_->coordinator->requestPlay(firstTrack);
                        // Stay on menu - PlaybackNavigationHandler will navigate when file is ready
                        return ScreenResult::stay();
                    }
                }
                return ScreenResult::stay();
            } else {
                // Action 1: "Clear queue"
                Serial.println("[MainMenu] Queue action: Clear queue");
                if (context_->queueManager) {
                    context_->queueManager->clear();
                }
                return ScreenResult::stay();
            }
        }
    }

    void drawItem(int itemIndex, int row, bool selected) override {
        if (itemIndex < 0 || itemIndex >= getItemCount()) return;

        const MenuItem& item = menuItems_[itemIndex];

        // Build display text with icon
        char itemText[100];
        if (itemIndex == 2 || itemIndex == 3) {
            // USB/Floppy - use text labels
            const char* label = (itemIndex == 2) ? "[USB]" : "[FLP]";
            snprintf(itemText, sizeof(itemText), "%s%s", label, item.label);
        } else {
            snprintf(itemText, sizeof(itemText), "%s%s", item.icon, item.label);
        }

        // DOS-style colors
        uint16_t fg = selected ? DOS_BLACK : DOS_WHITE;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;

        // Fill row background
        context_->ui->fillGridRect(4, row, 72, 1, bg);

        // Draw selection arrow if selected (DOS style)
        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);  // Right arrow
        }

        // Draw item text
        context_->ui->drawText(6, row, itemText, fg, bg);

        // Draw status on right side
        if (strlen(item.status) > 0) {
            uint16_t statusFg = selected ? DOS_BLACK : DOS_LIGHT_GRAY;
            context_->ui->drawText(60, row, item.status, statusFg, bg);
        }
    }

    ScreenResult onItemSelected(int itemIndex) override {
        if (itemIndex < 0 || itemIndex >= getItemCount()) {
            return ScreenResult::stay();
        }

        // Check if this item has actions - if so, delegate to base class
        int actionCount = 0;
        getItemActions(itemIndex, actionCount);
        if (actionCount > 0) {
            // Item has actions - use ActionableListScreenBase behavior (executes action)
            Serial.println("[MainMenu] Item has actions, delegating to base class");
            return ActionableListScreenBase::onItemSelected(itemIndex);
        }

        // Item has no actions - use custom navigation logic
        // Floppy not found - do nothing
        if (itemIndex == 3 && strcmp(menuItems_[3].status, "[Not Found]") == 0) {
            Serial.println("[MainMenu] Floppy not found - cannot navigate");
            return ScreenResult::stay();
        }

        // Navigate to target screen (USB always allowed now - will check inside browser)
        Serial.print("[MainMenu] Navigating to screen: ");
        Serial.println(menuItems_[itemIndex].targetScreen);
        return ScreenResult::navigateTo(menuItems_[itemIndex].targetScreen);
    }

    // ============================================
    // OVERRIDE DISPLAY METHODS
    // ============================================

    void drawHeader() override {
        // Draw window frame (DOS style)
        context_->ui->drawWindow(0, 0, 100, 30, " OPL3 MIDI PLAYER v1.0 ", DOS_WHITE, DOS_BLUE);

        // FUTURE: Show play status in top-right corner (feature enhancement)
        // context_->ui->drawText(60, 1, "♪ Now Playing: Song", DOS_YELLOW, DOS_BLUE);

        // Draw menu panel (DOS style)
        context_->ui->drawPanel(2, 3, 76, 20, " Main Menu ", DOS_WHITE, DOS_BLUE);
    }

    void drawFooter() override {
        // Horizontal separator (DOS style)
        context_->ui->drawHLine(0, 28, 100, DOS_WHITE);

        // Global status bar (shows "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->draw();
        }
    }

    void updateLCD() override {
        if (!context_->lcdManager) return;

        int itemIndex = selectedIndex_;
        if (itemIndex < 0 || itemIndex >= getItemCount()) {
            return;
        }

        // Check if this is the queue item (index 5 when shown)
        if (showQueueItem_ && itemIndex == 5) {
            // Queue item - show current action (updates frequently)
            const ItemAction* currentAction = getCurrentAction();
            if (currentAction) {
                PlaybackState* state = PlaybackState::getInstance();
                bool isPlaying = (state->getStatus() == PLAYBACK_PLAYING);

                // Line 1: Show current track if playing, or action description if stopped
                if (isPlaying && currentActionIndex_ == 0) {
                    // Show current track name (truncate to 16 chars)
                    String currentFile = state->getCurrentFile();
                    int lastSlash = currentFile.lastIndexOf('/');
                    if (lastSlash >= 0) {
                        currentFile = currentFile.substring(lastSlash + 1);
                    }
                    char truncated[17];
                    strncpy(truncated, currentFile.c_str(), 16);
                    truncated[16] = '\0';
                    context_->lcdManager->setLine(0, truncated);
                } else {
                    // Show action description
                    context_->lcdManager->setLine(0, currentAction->description);
                }

                // Line 2: Action with arrows and dot
                char line2[17];
                snprintf(line2, sizeof(line2), "%c%c %c%s",
                         LCD_CHAR_LEFT_ARROW, LCD_CHAR_RIGHT_ARROW,
                         LCD_CHAR_SELECT, currentAction->label);
                context_->lcdManager->setLine(1, line2);
            }
            return;
        }

        // Standard menu item (not queue) - show simple "Open" text
        context_->lcdManager->setLine(0, menuItems_[itemIndex].lcdHelp);
        context_->lcdManager->setLine(1, "Sel:Open");
    }

private:
    // ============================================
    // HELPER METHODS
    // ============================================

    /**
     * Update floppy drive status based on current state
     */
    void updateFloppyStatus() {
        if (context_->hasFloppy() && context_->floppy->isFloppyConnected()) {
            updateItemStatus(3, "[Ready]");
        } else {
            updateItemStatus(3, "[Not Found]");
        }
    }

    /**
     * Update menu item status dynamically (e.g., USB connected/disconnected)
     * Uses incremental update - NO BLANKING!
     */
    void updateItemStatus(int index, const char* newStatus) {
        if (index >= 0 && index < MAX_MENU_ITEMS) {
            strncpy(menuItems_[index].status, newStatus, sizeof(menuItems_[index].status) - 1);
            menuItems_[index].status[sizeof(menuItems_[index].status) - 1] = '\0';

            // Redraw the item (incremental update, no screen blanking)
            int row = startRow_ + index * itemSpacing_;
            bool isSelected = (index == selectedIndex_);
            drawItem(index, row, isSelected);

            Serial.print("[MainMenu] Updated item ");
            Serial.print(index);
            Serial.print(" status to: ");
            Serial.println(newStatus);
        }
    }

    /**
     * Update queue item state (show/hide, update actions)
     * Called on enter and when queue/playback events fire
     */
    void updateQueueItemState() {
        bool wasShown = showQueueItem_;
        char oldStatus[32] = "";

        // Save old status if item is shown
        if (showQueueItem_) {
            strncpy(oldStatus, menuItems_[5].status, sizeof(oldStatus) - 1);
            oldStatus[sizeof(oldStatus) - 1] = '\0';
        }

        PlaybackState* state = PlaybackState::getInstance();
        bool isPlaying = (state->getStatus() == PLAYBACK_PLAYING);
        bool hasQueue = (context_->queueManager && !context_->queueManager->isEmpty());

        // Show queue item if playing OR queue has tracks
        showQueueItem_ = isPlaying || hasQueue;

        if (showQueueItem_) {
            // Update queue item content based on state
            if (isPlaying) {
                // State 1: Playing - show "Now Playing"
                // Queue size = upcoming tracks only (not including current)
                int queueSize = context_->queueManager ? context_->queueManager->getQueueSize() : 0;

                menuItems_[5] = {" Now Playing", "\x0E", "", SCREEN_NOW_PLAYING, ""};

                if (queueSize > 0) {
                    snprintf(menuItems_[5].status, sizeof(menuItems_[5].status),
                             "[+%d queued]", queueSize);
                } else {
                    strcpy(menuItems_[5].status, "");  // No queue, just playing
                }

                // Update actions for playing state
                // Action 0: Go to Now Playing (default)
                // Action 1: Stop & Clear (clears queue, stops playback)
                queueActions_[0] = {"Now Playing", "Go to Now Playing"};
                queueActions_[1] = {"Stop & Clear", "Stop and clear queue"};

            } else {
                // State 2: Stopped with queue - show "Play Queue"
                int queueSize = context_->queueManager ? context_->queueManager->getQueueSize() : 0;

                menuItems_[5] = {" Play Queue", "\x10", "", SCREEN_NOW_PLAYING, ""};
                snprintf(menuItems_[5].status, sizeof(menuItems_[5].status),
                         "[%d tracks]", queueSize);

                // Update actions for stopped state
                // Action 0: Start playback (default)
                // Action 1: Clear queue
                queueActions_[0] = {"Start", "Start playback"};
                queueActions_[1] = {"Clear", "Clear queue"};
            }
        }

        // If visibility changed, only update that one item - don't redraw entire menu
        if (wasShown != showQueueItem_) {
            Serial.printf("[MainMenu] Queue item visibility changed: %d -> %d\n", wasShown, showQueueItem_);
            int row = startRow_ + 5 * itemSpacing_;

            if (!showQueueItem_) {
                // Hiding - just erase it
                context_->ui->fillGridRect(4, row, 72, 1, DOS_BLUE);
            } else {
                // Showing - draw the item
                bool selected = (5 == selectedIndex_);
                drawItem(5, row, selected);
            }
        }
        // If status changed, incrementally update just that item (no full redraw)
        else if (showQueueItem_ && strcmp(oldStatus, menuItems_[5].status) != 0) {
            Serial.printf("[MainMenu] Queue item status changed: '%s' -> '%s'\n", oldStatus, menuItems_[5].status);
            updateItemStatus(5, menuItems_[5].status);
        }
    }

    // ============================================
    // EVENT HANDLERS
    // ============================================

    static void onPlaybackStarted(void* userData) {
        MainMenuScreenNew* menu = static_cast<MainMenuScreenNew*>(userData);
        if (menu) {
            // Something started playing - update to show "Now Playing" item
            menu->updateQueueItemState();
        }
    }

    static void onPlaybackStopped(int stopReason, void* userData) {
        MainMenuScreenNew* menu = static_cast<MainMenuScreenNew*>(userData);
        if (menu) {
            Serial.printf("[MainMenu] onPlaybackStopped fired! reason=%d\n", stopReason);

            // Playback stopped - check if queue is empty
            bool queueIsEmpty = !menu->context_->queueManager ||
                               menu->context_->queueManager->isEmpty();

            if (queueIsEmpty) {
                // Queue is empty - hide the item immediately
                Serial.println("[MainMenu] Playback stopped, queue empty - hiding item");
                bool wasShown = menu->showQueueItem_;
                menu->showQueueItem_ = false;

                if (wasShown) {
                    // Just erase the item - no need to redraw entire menu
                    int row = menu->startRow_ + 5 * menu->itemSpacing_;
                    menu->context_->ui->fillGridRect(4, row, 72, 1, DOS_BLUE);
                }
            } else {
                // Queue still has tracks - this is auto-advance
                // Do nothing here - let the STARTED event update when new song begins
                // This avoids the momentary "Play Queue" flicker during auto-advance
                Serial.println("[MainMenu] Playback stopped, but queue has tracks - waiting for next song");
            }
        }
    }

    static void onQueueEvent(void* userData) {
        MainMenuScreenNew* menu = static_cast<MainMenuScreenNew*>(userData);
        if (menu) {
            menu->updateQueueItemState();
        }
    }

};

// Static action definitions (mutable - content changes based on state)
ActionableListScreenBase::ItemAction MainMenuScreenNew::queueActions_[2] = {
    {"Start playback", "Play first track"},
    {"Clear queue", "Remove all tracks"}
};

#endif // MAIN_MENU_SCREEN_NEW_H
