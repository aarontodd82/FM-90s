#include "status_bar_manager.h"

StatusBarManager::StatusBarManager(RetroUI* ui, EventManager* eventManager,
                                   PlaybackState* playbackState, QueueManager* queueManager)
    : ui_(ui)
    , eventManager_(eventManager)
    , playbackState_(playbackState)
    , queueManager_(queueManager)
    , currentScreen_(SCREEN_MAIN_MENU)
    , lastDrawnText_("")
    , needsRedraw_(true) {
}

StatusBarManager::~StatusBarManager() {
    // Unregister all event handlers
    if (eventManager_) {
        eventManager_->offAll(this);
    }
}

void StatusBarManager::begin() {
    // Register for playback events
    if (eventManager_) {
        eventManager_->on(EventManager::EVENT_PLAYBACK_STARTED, onPlaybackStarted, this);
        eventManager_->onInt(EventManager::EVENT_PLAYBACK_STOPPED_COMPLETE, onPlaybackStopped, this);
        eventManager_->on(EventManager::EVENT_QUEUE_CHANGED, onQueueChanged, this);
    }

    Serial.println("[StatusBarManager] Initialized with event listeners");
}

void StatusBarManager::setCurrentScreen(ScreenID screenID) {
    if (currentScreen_ != screenID) {
        currentScreen_ = screenID;
        needsRedraw_ = true;  // Redraw on screen change (content may differ)
        Serial.printf("[StatusBarManager] Screen changed to %d\n", (int)screenID);
    }
}

void StatusBarManager::draw() {
    if (!ui_) return;

    String statusText = buildStatusText();

    // Draw status bar background
    ui_->fillGridRect(0, 29, 100, 1, DOS_LIGHT_GRAY);

    // Draw status text on left (if not empty)
    if (statusText.length() > 0) {
        ui_->drawText(1, 29, statusText.c_str(), DOS_BLACK, DOS_LIGHT_GRAY);
    }

    // Update last drawn text for dirty checking
    lastDrawnText_ = statusText;
    needsRedraw_ = false;

    Serial.printf("[StatusBarManager] Drew status bar: '%s'\n", statusText.c_str());
}

bool StatusBarManager::update() {
    if (!ui_) return false;

    // Check if content changed or redraw requested
    String currentText = buildStatusText();

    if (needsRedraw_ || currentText != lastDrawnText_) {
        // Content changed - redraw incrementally

        // Only clear and redraw the left portion (leave notifications alone)
        int maxTextLength = 100 - MAX_NOTIFICATION_LENGTH;

        // Clear old text area
        ui_->fillGridRect(0, 29, maxTextLength, 1, DOS_LIGHT_GRAY);

        // Draw new text
        if (currentText.length() > 0) {
            ui_->drawText(1, 29, currentText.c_str(), DOS_BLACK, DOS_LIGHT_GRAY);
        }

        lastDrawnText_ = currentText;
        needsRedraw_ = false;

        return true;  // Redraw occurred
    }

    return false;  // No redraw needed
}

void StatusBarManager::requestRedraw() {
    needsRedraw_ = true;
}

String StatusBarManager::buildStatusText() {
    String result = "";

    // Get current playback state
    bool isPlaying = (playbackState_ && playbackState_->getStatus() == PLAYBACK_PLAYING);
    bool hasQueue = (queueManager_ && !queueManager_->isEmpty());

    // Part 1: "Now playing:" (everywhere except Now Playing screen)
    if (isPlaying && currentScreen_ != SCREEN_NOW_PLAYING) {
        String currentFile = playbackState_->getCurrentFile();
        String filename = getFilenameFromPath(currentFile);

        if (filename.length() > 0) {
            result += "Now: " + filename;
        }
    }

    // Part 2: "Next:" (everywhere, including Now Playing screen)
    // IMPORTANT: Use getCurrentTrack() not getNextTrack()!
    // queue_[0] = next track to play (currently playing song is NOT in queue)
    // queue_[1] = track after that (getNextTrack returns this - WRONG for "Up Next")
    if (hasQueue) {
        const char* nextTrack = queueManager_->getCurrentTrack();
        if (nextTrack && strlen(nextTrack) > 0) {
            String nextFilename = getFilenameFromPath(String(nextTrack));

            // Add separator if we already have "Now:"
            if (result.length() > 0) {
                result += "  |  ";
            }

            result += "Next: " + nextFilename;
        }
    }

    // Truncate to fit available space (accounting for notifications)
    int maxLength = 100 - MAX_NOTIFICATION_LENGTH - 2;  // -2 for padding
    if (result.length() > maxLength) {
        result = truncateText(result, maxLength);
    }

    return result;
}

String StatusBarManager::truncateText(const String& text, int maxLength) {
    if (text.length() <= maxLength) {
        return text;
    }

    // Truncate and add ellipsis
    return text.substring(0, maxLength - 3) + "...";
}

String StatusBarManager::getFilenameFromPath(const String& path) {
    if (path.length() == 0) return "";

    int lastSlash = path.lastIndexOf('/');
    if (lastSlash >= 0 && lastSlash < path.length() - 1) {
        return path.substring(lastSlash + 1);
    }

    return path;  // No slash found, return as-is
}

// ============================================
// EVENT CALLBACKS
// ============================================

void StatusBarManager::onPlaybackStarted(void* userData) {
    StatusBarManager* manager = static_cast<StatusBarManager*>(userData);
    if (manager) {
        Serial.println("[StatusBarManager] Playback started - requesting redraw");
        manager->needsRedraw_ = true;
    }
}

void StatusBarManager::onPlaybackStopped(int stopReason, void* userData) {
    StatusBarManager* manager = static_cast<StatusBarManager*>(userData);
    if (manager) {
        Serial.println("[StatusBarManager] Playback stopped - requesting redraw");
        manager->needsRedraw_ = true;
    }
}

void StatusBarManager::onQueueChanged(void* userData) {
    StatusBarManager* manager = static_cast<StatusBarManager*>(userData);
    if (manager) {
        Serial.println("[StatusBarManager] Queue changed - requesting redraw");
        manager->needsRedraw_ = true;
    }
}
