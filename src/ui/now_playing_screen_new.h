#ifndef NOW_PLAYING_SCREEN_NEW_H
#define NOW_PLAYING_SCREEN_NEW_H

#include "framework/action_cycling_screen_base.h"
#include "framework/playback_navigation_handler.h"
#include "framework/status_bar_manager.h"
#include "screen_id.h"
#include "../dos_colors.h"
#include "../playback_state.h"
#include "../opl_register_log.h"
#include "../opl3_synth.h"
#include "../player_manager.h"  // For unified player management
#include "../playback_coordinator.h"  // For requestStop()
#include "../queue_manager.h"  // For queue navigation
#include "../fm9_player.h"  // For cover image access
#include "../fm9_file.h"    // For FM9_IMAGE_WIDTH/HEIGHT constants
#include "RA8875_SPI1.h"    // For direct TFT access (cover image)
#include "lcd_symbols.h"
#include "../debug_config.h"  // For DEBUG_SERIAL_ENABLED and DEBUG_PERFORMANCE_STATS

/**
 * NowPlayingScreenNew - Real-time playback display using new framework
 *
 * Features:
 * - File info (name, format, elapsed/total time)
 * - Real-time OPL register stream (format-agnostic visualization)
 * - Voice activity stats (2-op, 4-op, drums)
 * - Progress bar
 * - Actions: Stop, Volume, Browse (go back but keep playing)
 *
 * Performance Optimizations:
 * - Multi-rate updates: 1Hz for progress, 10Hz for register stream
 * - Incremental rendering: only draws changed elements
 * - Waterfall scrolling: draws only 1 new line per update
 *
 * Framework Usage:
 * - Extends ActionCyclingScreenBase for automatic action cycling
 * - LEFT/RIGHT to cycle actions, SELECT to execute
 * - UP/DOWN for register list scrolling
 */
class NowPlayingScreenNew : public ActionCyclingScreenBase {
private:
    enum ActionID {
        ACTION_STOP = 0,
        ACTION_BROWSE = 1,
        ACTION_NEXT = 2
    };

    // Action definitions for ActionCyclingScreenBase (dynamic based on queue state)
    Action actions_[3];  // Max 3 actions: Stop, Browse, Next
    int actionCount_;    // Actual count (2-3 depending on queue)

    int registerScrollOffset_;  // For scrolling through register list

    // Multi-rate update timing - different elements update at different frequencies
    uint32_t lastInfoUpdate_;        // Progress bar + Time display: 1Hz (1000ms)
    uint32_t lastRegisterUpdate_;    // Register stream: 10Hz (100ms) - smooth scrolling
    uint32_t lastLogCount_;          // Track how many log entries we've seen
    int currentDisplayRow_;          // Current row for scrolling waterfall effect (14-26)

    // Performance monitoring
    uint32_t maxUpdateTime_;         // Track worst-case update time
    uint32_t updateCount_;           // Count total updates for averaging

    // Dirty checking - avoid redrawing unchanged content
    char lastTimeString_[60];        // Track last time string to detect changes

    // Cover image state
    bool hasCoverImage_;             // True if current track has FM9 cover image

public:
    NowPlayingScreenNew(ScreenContext* context)
        : ActionCyclingScreenBase(context),
          actionCount_(0),
          registerScrollOffset_(0),
          lastInfoUpdate_(0),
          lastRegisterUpdate_(0),
          lastLogCount_(0),
          currentDisplayRow_(14),  // Start at top row
          maxUpdateTime_(0),
          updateCount_(0),
          hasCoverImage_(false) {
        lastTimeString_[0] = '\0';  // Initialize empty for dirty checking
        updateAvailableActions();
    }

    // ============================================
    // LIFECYCLE METHODS
    // ============================================

    void onCreate(void* params) override {
        // Listen for playback start to refresh file info
        if (context_ && context_->eventManager) {
            context_->eventManager->on(EventManager::EVENT_PLAYBACK_STARTED, onPlaybackStarted, this);
            // Listen for queue changes to update "Next:" display and actions
            context_->eventManager->on(EventManager::EVENT_QUEUE_CHANGED, onQueueChanged, this);
        }
        updateAvailableActions();
    }

    void onEnter() override {
        updateAvailableActions();
        lastTimeString_[0] = '\0';  // Reset dirty check to force initial draw
        ActionCyclingScreenBase::onEnter();
    }

    void onDestroy() override {
        // Unregister event handlers
        if (context_ && context_->eventManager) {
            context_->eventManager->offAll(this);
        }
    }

    // ============================================
    // DISPLAY METHODS
    // ============================================

    void draw() override {
        Serial.println("[NowPlaying] draw: Starting");
        if (!context_->ui) return;

        // Check for cover image before drawing layout
        checkForCoverImage();

        // Draw main window (drawWindow fills the background automatically)
        Serial.println("[NowPlaying] draw: Drawing window");
        context_->ui->drawWindow(0, 0, 100, 30, " NOW PLAYING ", DOS_WHITE, DOS_BLUE);

        // Draw cover image if available (must be before file info panel)
        if (hasCoverImage_) {
            Serial.println("[NowPlaying] draw: Drawing cover image");
            drawCoverImage();
        }

        // Redraw all components
        Serial.println("[NowPlaying] draw: Drawing file info");
        drawFileInfo();

        Serial.println("[NowPlaying] draw: Drawing progress bar");
        drawProgressBar();

        Serial.println("[NowPlaying] draw: Drawing OPL register stream");
        drawOPLRegisterStream();

        Serial.println("[NowPlaying] draw: Drawing footer");
        drawFooter();

        // Initialize all update timers
        Serial.println("[NowPlaying] draw: Initializing timers");
        uint32_t now = millis();
        lastInfoUpdate_ = now;
        lastRegisterUpdate_ = now;

        Serial.println("[NowPlaying] draw: Complete");
    }

    void update() override {
        if (!context_->ui) return;

        // Update global status bar (dynamic "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->update();
        }

        uint32_t now = millis();
        uint32_t updateStart = micros();
        bool didUpdate = false;

        // Multi-rate updates: Different UI elements update at different frequencies
        // This spreads the rendering load across multiple frames instead of doing
        // everything at once, reducing peak CPU usage and audio interference.

        // Progress bar + Time string: 1Hz (1000ms) - Update together since both are 1Hz
        if (now - lastInfoUpdate_ >= 1000) {
            updateFileInfoData();
            updateProgressBarData();
            lastInfoUpdate_ = now;
            didUpdate = true;
        }

        // Register stream: 10Hz (100ms) - Fast, smooth scrolling (only draws 1 line!)
        if (now - lastRegisterUpdate_ >= 100) {
            updateOPLRegisterStreamData();
            lastRegisterUpdate_ = now;
            didUpdate = true;
        }

        // Performance monitoring - track update times
        if (didUpdate) {
            uint32_t updateDuration = micros() - updateStart;
            updateCount_++;

            if (updateDuration > maxUpdateTime_) {
                maxUpdateTime_ = updateDuration;
            }

            #if DEBUG_SERIAL_ENABLED && DEBUG_PERFORMANCE_STATS
            // Log if any update takes >5ms (potential audio interference)
            if (updateDuration > 5000) {
                // // Serial.printf("[NowPlaying] WARNING: Screen update took %lu us (%.2f ms) - may affect audio timing\n",
                             updateDuration, updateDuration / 1000.0f);
            }

            // Every 100 updates, report performance stats
            if (updateCount_ % 100 == 0) {
                // // Serial.printf("[NowPlaying] Performance: max update time = %.2f ms over last 100 updates\n",
                             maxUpdateTime_ / 1000.0f);
                maxUpdateTime_ = 0;  // Reset for next 100 updates
            }
            #else
            // Still track max time even if not logging
            if (updateCount_ % 100 == 0) {
                maxUpdateTime_ = 0;  // Reset for next 100 updates
            }
            #endif
        }
    }

    // ============================================
    // ACTION CYCLING SCREEN BASE IMPLEMENTATION
    // ============================================

    const Action* getActions() override {
        return actions_;
    }

    int getActionCount() override {
        return actionCount_;
    }

    ScreenResult onActionExecuted(int actionIndex, int actionID) override {
        return executeAction(actionID);
    }

    // ============================================
    // INPUT HANDLING (UP/DOWN for register scrolling)
    // ============================================

    ScreenResult onUp() override {
        // Scroll OPL register list up
        if (registerScrollOffset_ > 0) registerScrollOffset_--;
        requestRedraw();
        return ScreenResult::stay();
    }

    ScreenResult onDown() override {
        // Scroll OPL register list down
        registerScrollOffset_++;
        requestRedraw();
        return ScreenResult::stay();
    }

private:
    // ============================================
    // DRAWING HELPERS
    // ============================================

    void drawFileInfo() {
        PlaybackState* state = PlaybackState::getInstance();

        // Panel for file info - position depends on cover image
        // Cover image is 100x100 pixels = ~13 cols x 7 rows at 8x16 font
        // If we have cover image: panel starts at col 16, otherwise col 2
        int panelStartCol = hasCoverImage_ ? 16 : 2;
        int panelWidth = hasCoverImage_ ? 82 : 96;

        context_->ui->drawPanel(panelStartCol, 2, panelWidth, 6, " Track Info ", DOS_WHITE, DOS_BLUE);

        String filename = state->getCurrentFile();
        if (filename.length() == 0) filename = "(No file playing)";

        // Center the filename within the panel
        int panelCenter = panelStartCol + (panelWidth / 2);
        int nameCol = panelCenter - (filename.length() / 2);
        if (nameCol < panelStartCol + 2) nameCol = panelStartCol + 2;
        context_->ui->drawText(nameCol, 4, filename.c_str(), DOS_BRIGHT_CYAN, DOS_BLUE);

        // Format and elapsed/total time - center in panel
        char infoBuf[60];
        snprintf(infoBuf, sizeof(infoBuf), "Format: %s", state->getFormatName());
        int infoCol = panelStartCol + 4;
        context_->ui->drawText(infoCol, 5, infoBuf, DOS_WHITE, DOS_BLUE);

        snprintf(infoBuf, sizeof(infoBuf), "Time: %s / %s",
                 state->getElapsedTimeString().c_str(),
                 state->getDurationString().c_str());
        context_->ui->drawText(infoCol, 6, infoBuf, DOS_WHITE, DOS_BLUE);
    }

    // Data-only update for file info (just the time string)
    void updateFileInfoData() {
        PlaybackState* state = PlaybackState::getInstance();

        // Build the new time string
        char infoBuf[60];
        snprintf(infoBuf, sizeof(infoBuf), "Time: %s / %s",
                 state->getElapsedTimeString().c_str(),
                 state->getDurationString().c_str());

        // Calculate column position (matches drawFileInfo)
        int panelStartCol = hasCoverImage_ ? 16 : 2;
        int infoCol = panelStartCol + 4;

        // Dirty checking: Only redraw if content changed
        if (strcmp(infoBuf, lastTimeString_) != 0) {
            context_->ui->drawText(infoCol, 6, infoBuf, DOS_WHITE, DOS_BLUE);
            strncpy(lastTimeString_, infoBuf, sizeof(lastTimeString_) - 1);
            lastTimeString_[sizeof(lastTimeString_) - 1] = '\0';
        }
    }

    void drawProgressBar() {
        PlaybackState* state = PlaybackState::getInstance();

        // Panel for progress
        context_->ui->drawPanel(2, 8, 96, 3, " Playback ", DOS_WHITE, DOS_BLUE);

        float progress = state->getProgress();
        context_->ui->drawProgressBar(4, 9, 92, progress, DOS_LIGHT_GRAY, DOS_BLUE);
    }

    // Data-only update (no panels/borders) for live updates
    void updateProgressBarData() {
        PlaybackState* state = PlaybackState::getInstance();
        float progress = state->getProgress();
        context_->ui->drawProgressBar(4, 9, 92, progress, DOS_LIGHT_GRAY, DOS_BLUE);
    }

    void drawOPLRegisterStream() {
        // Panel for real-time OPL register stream (more vertical space now)
        context_->ui->drawPanel(2, 11, 96, 16, " OPL Register Stream (Live) ", DOS_WHITE, DOS_BLUE);

        // Column headers (ALL THE INFO!)
        context_->ui->drawText(4, 12, "C Reg Val Binary   Name         Ch Op Time  Decoded", DOS_BRIGHT_CYAN, DOS_BLUE);
        context_->ui->drawHLine(3, 13, 94, DOS_WHITE);

        // Get recent register writes
        OPLRegisterWrite recentWrites[30];
        int count = g_oplLog.getRecent(recentWrites, 30);

        // Display up to 13 rows (more vertical space without voice activity)
        int visibleRows = min(13, count);
        for (int i = 0; i < visibleRows; i++) {
            int idx = i + registerScrollOffset_;
            if (idx >= count) break;

            const OPLRegisterWrite& write = recentWrites[idx];
            int row = 14 + i;

            char lineBuf[80];
            int channel = write.getChannel();

            // Simplified format: just Reg, Value, Name, Channel (no chip, no time)
            if (channel >= 0) {
                snprintf(lineBuf, sizeof(lineBuf), "     %03X   %02X   %-12s %2d",
                         write.reg, write.value, write.getRegisterName(), channel);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "     %03X   %02X   %-12s  -",
                         write.reg, write.value, write.getRegisterName());
            }

            // Color code by register type
            uint16_t color = DOS_WHITE;
            if (strstr(write.getRegisterName(), "FREQ")) color = DOS_BRIGHT_CYAN;
            else if (strstr(write.getRegisterName(), "ON")) color = DOS_BRIGHT_GREEN;
            else if (strstr(write.getRegisterName(), "LEVEL")) color = DOS_YELLOW;
            else if (strstr(write.getRegisterName(), "ATTACK")) color = DOS_PINK;

            context_->ui->drawText(4, row, lineBuf, color, DOS_BLUE);
        }

        // Show scroll indicator if more entries available
        if (count > 13) {
            char scrollBuf[20];
            snprintf(scrollBuf, sizeof(scrollBuf), "(%d more...)", count - visibleRows);
            context_->ui->drawText(75, 27, scrollBuf, DOS_DARK_GRAY, DOS_BLUE);
        }

        // Show write rate
        char rateBuf[30];
        snprintf(rateBuf, sizeof(rateBuf), "%lu writes/sec", g_oplLog.getWritesPerSecond());
        context_->ui->drawText(4, 27, rateBuf, DOS_LIGHT_GRAY, DOS_BLUE);
    }

    // Data-only update - Draw ONLY ONE new entry per update (minimal drawing)
    void updateOPLRegisterStreamData() {
        // Get total count of log entries
        uint32_t currentLogCount = g_oplLog.getTotalWrites();

        // If no new entries, nothing to do
        if (currentLogCount <= lastLogCount_) {
            return;
        }

        // Get the most recent writes
        OPLRegisterWrite recentWrites[30];
        int count = g_oplLog.getRecent(recentWrites, 30);

        if (count == 0) {
            lastLogCount_ = currentLogCount;
            return;
        }

        // Draw ONLY the newest entry (index 0)
        const OPLRegisterWrite& write = recentWrites[0];

        // Use rotating row index (creates scrolling waterfall effect)
        // Rows 14-26 = 13 visible rows
        int row = currentDisplayRow_;
        currentDisplayRow_++;
        if (currentDisplayRow_ > 26) {
            currentDisplayRow_ = 14;  // Wrap around to top
        }

        // Build the FULL info line with ALL details
        char lineBuf[120];  // Longer buffer for all the info
        int channel = write.getChannel();
        int op = write.getOperator();

        // Convert value to binary string
        char binary[9];
        for (int i = 0; i < 8; i++) {
            binary[7-i] = (write.value & (1 << i)) ? '1' : '0';
        }
        binary[8] = '\0';

        // Get decoded information
        char decoded[60];
        write.getDecoded(decoded, sizeof(decoded));

        // Calculate relative timestamp in seconds (from start)
        float timeSec = (write.timestamp - g_oplLog.getFirstTimestamp()) / 1000.0f;

        // Format: C Reg Val Binary   Name         Ch Op Time  Decoded
        char chStr[3] = "--";
        if (channel >= 0) snprintf(chStr, sizeof(chStr), "%2d", channel);

        char opStr[3] = "- ";
        if (op >= 0) snprintf(opStr, sizeof(opStr), "%2d", op);

        snprintf(lineBuf, sizeof(lineBuf), "%d %03X %02X %s %-12s %2s %2s %5.2f %s",
                 write.chip, write.reg, write.value, binary,
                 write.getRegisterName(), chStr, opStr, timeSec, decoded);

        // Color code by register type
        uint16_t color = DOS_WHITE;
        if (strstr(write.getRegisterName(), "FREQ")) color = DOS_BRIGHT_CYAN;
        else if (strstr(write.getRegisterName(), "ON")) color = DOS_BRIGHT_GREEN;
        else if (strstr(write.getRegisterName(), "LEVEL")) color = DOS_YELLOW;
        else if (strstr(write.getRegisterName(), "ATTACK")) color = DOS_PINK;

        // Clear the entire line first (92 columns wide, starting at col 4)
        context_->ui->fillGridRect(4, row, 92, 1, DOS_BLUE);

        // Draw ONLY this one new line
        context_->ui->drawText(4, row, lineBuf, color, DOS_BLUE);

        // Mark that we've displayed this entry
        lastLogCount_ = currentLogCount;
    }

    void drawFooter() {
        context_->ui->drawHLine(0, 28, 100, DOS_WHITE);

        // Global status bar (shows "Next:" - "Now:" hidden on this screen)
        if (context_->statusBarManager) {
            context_->statusBarManager->draw();
        }
    }

    // ============================================
    // COVER IMAGE HELPERS
    // ============================================

    /**
     * Check if current player has a cover image
     * Must be called before layout decisions
     */
    void checkForCoverImage() {
        hasCoverImage_ = false;

        if (!context_->playerManager) return;

        IAudioPlayer* player = context_->playerManager->getCurrentPlayer();
        if (!player) return;

        // Only FM9 format has cover images
        if (player->getFormat() == FileFormat::FM9) {
            FM9Player* fm9Player = static_cast<FM9Player*>(player);
            hasCoverImage_ = fm9Player->hasCoverImage();
            if (hasCoverImage_) {
                Serial.println("[NowPlaying] FM9 cover image detected");
            }
        }
    }

    /**
     * Draw the cover image using RA8875's drawImage function
     * Image is 100x100 RGB565, displayed at top-left corner
     */
    void drawCoverImage() {
        if (!context_->playerManager) return;

        IAudioPlayer* player = context_->playerManager->getCurrentPlayer();
        if (!player || player->getFormat() != FileFormat::FM9) return;

        FM9Player* fm9Player = static_cast<FM9Player*>(player);
        const uint16_t* imageData = fm9Player->getCoverImage();
        if (!imageData) return;

        // Get the TFT display from RetroUI
        // The image position in pixels:
        // - Row 2 in grid = 2 * 16 = 32 pixels from top (inside window border)
        // - Col 2 in grid = 2 * 8 = 16 pixels from left (inside window border)
        // Add a small offset for visual padding
        int pixelX = 16 + 4;  // 2 cols * 8 + 4px padding
        int pixelY = 32 + 4;  // 2 rows * 16 + 4px padding

        RA8875_SPI1* tft = context_->ui->getTFT();
        if (tft) {
            tft->drawImage(pixelX, pixelY, FM9_IMAGE_WIDTH, FM9_IMAGE_HEIGHT, imageData);
        }
    }

    // ============================================
    // EVENT HANDLERS
    // ============================================

    static void onPlaybackStarted(void* userData) {
        NowPlayingScreenNew* screen = static_cast<NowPlayingScreenNew*>(userData);
        if (!screen) return;

        // Playback started - redraw file info to show correct filename/format
        Serial.println("[NowPlaying] EVENT_PLAYBACK_STARTED received, refreshing file info");
        screen->drawFileInfo();
    }

    static void onQueueChanged(void* userData) {
        NowPlayingScreenNew* screen = static_cast<NowPlayingScreenNew*>(userData);
        if (!screen) return;

        // Queue changed - update actions (status bar "Next:" updates automatically via StatusBarManager)
        screen->updateAvailableActions();
    }

    // ============================================
    // ACTION EXECUTION
    // ============================================

    ScreenResult executeAction(int actionID) {
        switch (actionID) {
            case ACTION_STOP:
                // Request stop through coordinator (user-initiated)
                // Coordinator handles:
                // - Stopping playback asynchronously
                // - Waiting for all audio cleanup to complete
                // - Navigation (via PlaybackNavigationHandler)
                context_->coordinator->requestStop(StopReason::USER_REQUEST);
                return ScreenResult::stay();  // Navigation handler will decide where to go

            case ACTION_BROWSE:
                // Go back to file browser but keep playing
                // Notify handler that user left intentionally (don't auto-navigate back on queue advance)
                PlaybackNavigationHandler::notifyUserLeftNowPlaying();
                return ScreenResult::goBack();

            case ACTION_NEXT:
                // Request next track from queue
                if (context_->coordinator && context_->queueManager && context_->queueManager->hasNext()) {
                    context_->coordinator->requestNext();
                }
                return ScreenResult::stay();  // Stay on Now Playing, new track will load

            default:
                return ScreenResult::stay();
        }
    }

    /**
     * Update available actions based on queue state
     * Rebuilds actions_ array to only include available actions
     */
    void updateAvailableActions() {
        actionCount_ = 0;

        // Always available: Stop and Browse
        actions_[actionCount_++] = {"Stop", "Stop playback", ACTION_STOP};
        actions_[actionCount_++] = {"Browse", "Browse files", ACTION_BROWSE};

        // Conditional: Next (if queue has next track)
        if (context_->queueManager && context_->queueManager->hasNext()) {
            actions_[actionCount_++] = {"Next", "Next track", ACTION_NEXT};
        }

        Serial.printf("[NowPlaying] Updated actions: %d available\n", actionCount_);
    }
};

#endif // NOW_PLAYING_SCREEN_NEW_H
