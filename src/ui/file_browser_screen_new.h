#ifndef FILE_BROWSER_SCREEN_NEW_H
#define FILE_BROWSER_SCREEN_NEW_H

#include "framework/actionable_list_screen_base.h"
#include "framework/event_manager.h"
#include "framework/playback_navigation_handler.h"
#include "framework/status_bar_manager.h"
#include "screen_manager.h"  // For getPreviousScreenID()
#include "lcd_symbols.h"
#include "../dos_colors.h"
#include "../playback_state.h"
#include "../file_source.h"
#include "../floppy_manager.h"
#include "../player_manager.h"  // For unified player management
#include "../playback_coordinator.h"  // For requestPlay()
#include "../queue_manager.h"  // For queue operations
#include "../opl3_synth.h"
#include <SD.h>
#include <vector>
#include "screen_id.h"
#include "../usb_drive_manager.h"

/**
 * FileBrowserScreenNew - File browser using new framework
 *
 * Features:
 * - Uses ActionableListScreenBase for multi-action support
 * - Event-driven updates (USB connect/disconnect, floppy ready)
 * - LoadingOverlay for async operations
 * - Multi-source support (SD, USB, Floppy) via ScreenContext
 * - Preserves original DOS styling
 */
class FileBrowserScreenNew : public ActionableListScreenBase {
public:
    enum FileSourceType {
        SOURCE_SD,
        SOURCE_USB,
        SOURCE_FLOPPY
    };

private:
    struct FileEntry {
        String name;
        bool isDirectory;
        uint32_t size;
        String type;  // "MIDI", "VGM", etc.
    };

    // Static cache for SD card directory (survives screen deletion)
    struct SDDirectoryCache {
        std::vector<FileEntry> files;
        String path;
        bool valid;
    };
    static SDDirectoryCache sdCache_;

    // Instance variables for current view state
    std::vector<FileEntry> files_;
    String currentPath_;
    FileSourceType sourceType_;
    bool loadingFloppyFiles_;  // True when waiting for async floppy file list

    // Actions for folders (generic)
    static ItemAction folderActions_[2];

    // Actions for SD folders (with queue support)
    static ItemAction sdFolderActions_[3];

    // Actions for files
    static ItemAction fileActions_[4];

    // Actions for USB files (with Refresh like floppy)
    static ItemAction usbFileActions_[3];
    static ItemAction usbBackActions_[2];

    // Actions for floppy files
    static ItemAction floppyFileActions_[3];

    // Back actions
    static ItemAction backAction_[1];
    static ItemAction floppyBackActions_[2];

public:
    FileBrowserScreenNew(ScreenContext* context, FileSourceType sourceType)
        : ActionableListScreenBase(context, 20, 5, 1),  // 20 visible items, start at row 5, compact spacing
          sourceType_(sourceType),
          loadingFloppyFiles_(false) {

        // Initialize current path based on source
        currentPath_ = "/";
    }

    virtual ~FileBrowserScreenNew() {
    }

    // ============================================
    // LIFECYCLE
    // ============================================

    void onCreate(void* params) override {
        // Register for USB events (if USB browser)
        if (sourceType_ == SOURCE_USB && context_->eventManager) {
            context_->eventManager->on(EventManager::EVENT_USB_CONNECTED, onUSBConnected, this);
            context_->eventManager->on(EventManager::EVENT_USB_DISCONNECTED, onUSBDisconnected, this);
        }

        // FUTURE: Register for floppy events when FloppyManager supports them (feature enhancement)
    }

    void onEnter() override {
        // For floppy, always clear cache to force refresh when entering screen
        if (sourceType_ == SOURCE_FLOPPY && context_->hasFloppy()) {
            context_->floppy->clearFileListCache();
        }

        // Determine if we need to reload the directory
        bool needsReload = false;
        const char* reloadReason = "";

        // Get previous screen to determine navigation context
        ScreenID previousScreen = context_->screenManager->getPreviousScreenID();

        // Check static SD cache first
        bool canUseSDCache = (sourceType_ == SOURCE_SD && sdCache_.valid && sdCache_.path == currentPath_);

        if (sourceType_ == SOURCE_FLOPPY || sourceType_ == SOURCE_USB) {
            // USB/Floppy can be unplugged or changed anytime - always reload
            needsReload = true;
            reloadReason = "dynamic-source";
        } else if (sourceType_ == SOURCE_SD && previousScreen == SCREEN_MAIN_MENU) {
            // Coming from main menu to SD card browser - reload because user might have just transferred files
            needsReload = true;
            reloadReason = "from-main-menu";
        } else if (sourceType_ == SOURCE_SD && previousScreen == SCREEN_NOW_PLAYING && canUseSDCache) {
            // Coming from now playing to SD card browser - use static cache (no transfers happened during playback)
            needsReload = false;
            reloadReason = "from-now-playing-cached";
        } else if (sourceType_ == SOURCE_SD && canUseSDCache) {
            // Default for SD: use static cache if available
            needsReload = false;
            reloadReason = "default-cache";
        } else {
            // No valid cache - need to load
            needsReload = true;
            reloadReason = canUseSDCache ? "unknown" : "no-cache";
        }

        if (needsReload) {
            Serial.printf("[FileBrowser] Loading directory (source=%d, path=%s, reason=%s)\n",
                         sourceType_, currentPath_.c_str(), reloadReason);
            loadDirectory();
        } else {
            // Use static SD cache
            Serial.printf("[FileBrowser] Using static SD cache (source=%d, path=%s, reason=%s)\n",
                         sourceType_, currentPath_.c_str(), reloadReason);
            files_ = sdCache_.files;
        }

        // Call base class
        ActionableListScreenBase::onEnter();
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

        // Check if floppy file list is ready
        if (loadingFloppyFiles_ && sourceType_ == SOURCE_FLOPPY) {
            if (context_->hasFloppy() && !context_->floppy->isRequestingFileList()) {
                // Load floppy files
                loadFloppyFiles();
                loadingFloppyFiles_ = false;

                // Clear status notification
                context_->ui->showStatusNotification("", 0, DOS_WHITE, DOS_BLUE);

                // Redraw
                requestRedraw();
            }
        }

        // Call base class
        ActionableListScreenBase::update();
    }

    // ============================================
    // LIST SCREEN BASE IMPLEMENTATION
    // ============================================

    int getItemCount() override {
        return files_.size();
    }

    void drawItem(int itemIndex, int row, bool selected) override {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) return;

        const FileEntry& file = files_[itemIndex];

        // DOS-style colors
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;
        uint16_t fg;

        if (file.type == "BACK") {
            // Back item
            fg = selected ? DOS_BLACK : DOS_YELLOW;
        } else if (file.isDirectory) {
            // Directory
            fg = selected ? DOS_BLACK : DOS_BRIGHT_CYAN;
        } else {
            // File
            fg = selected ? DOS_BLACK : DOS_WHITE;
        }

        // Fill row background
        context_->ui->fillGridRect(2, row, 96, 1, bg);

        // Draw selection arrow if selected (DOS style)
        if (selected) {
            context_->ui->drawText(2, row, "\x10", DOS_BLACK, DOS_CYAN);  // Right arrow
        }

        // Draw icon
        const char* icon = file.isDirectory ? "\xFE" : " ";  // Folder icon or space
        if (file.type == "BACK") {
            icon = "\x11";  // Left arrow for back
        }
        context_->ui->drawText(4, row, icon, fg, bg);

        // Draw file/folder name (truncated to fit)
        char displayName[40];
        snprintf(displayName, sizeof(displayName), "%.38s", file.name.c_str());
        context_->ui->drawText(6, row, displayName, fg, bg);

        // Draw file type
        if (!file.isDirectory && file.type != "BACK") {
            context_->ui->drawText(45, row, file.type.c_str(),
                                  selected ? DOS_BLACK : DOS_LIGHT_GRAY, bg);
        }

        // Draw file size
        if (!file.isDirectory && file.size > 0) {
            char sizeStr[16];
            formatFileSize(file.size, sizeStr, sizeof(sizeStr));
            context_->ui->drawText(55, row, sizeStr,
                                  selected ? DOS_BLACK : DOS_LIGHT_GRAY, bg);
        }
    }

    // ============================================
    // ACTIONABLE LIST IMPLEMENTATION
    // ============================================

    const ItemAction* getItemActions(int itemIndex, int& count) override {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            count = 0;
            return nullptr;
        }

        const FileEntry& file = files_[itemIndex];

        // Back item
        if (file.type == "BACK") {
            if (sourceType_ == SOURCE_FLOPPY) {
                count = 2;
                return floppyBackActions_;
            } else if (sourceType_ == SOURCE_USB) {
                count = 2;
                return usbBackActions_;
            } else {
                count = 1;
                return backAction_;
            }
        }

        // Folder - SD folders have queue support
        if (file.isDirectory) {
            if (sourceType_ == SOURCE_SD) {
                count = 3;  // Open, Add to queue, Go back
                return sdFolderActions_;
            } else {
                count = 2;  // Open, Go back (no queue for USB/Floppy)
                return folderActions_;
            }
        }

        // File - different actions based on source
        if (sourceType_ == SOURCE_USB) {
            count = 3;  // Play, Move to SD, Refresh
            return usbFileActions_;
        } else if (sourceType_ == SOURCE_FLOPPY) {
            count = 3;
            return floppyFileActions_;
        } else {
            count = 4;
            return fileActions_;
        }
    }

    ScreenResult onActionExecuted(int itemIndex, int actionIndex) override {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            return ScreenResult::stay();
        }

        const FileEntry& file = files_[itemIndex];

        // Get actions for this item
        int actionCount = 0;
        const ItemAction* actions = getItemActions(itemIndex, actionCount);
        if (!actions || actionIndex >= actionCount) {
            return ScreenResult::stay();
        }

        const char* actionLabel = actions[actionIndex].label;

        // Handle action based on label
        if (strcmp(actionLabel, "Open folder") == 0) {
            return openFolder(itemIndex);
        } else if (strcmp(actionLabel, "Go back") == 0) {
            return goBack();
        } else if (strcmp(actionLabel, "Refresh") == 0) {
            // Handle refresh for different sources
            if (sourceType_ == SOURCE_FLOPPY && context_->hasFloppy()) {
                context_->floppy->clearFileListCache();
            } else if (sourceType_ == SOURCE_USB && context_->hasUSBDrive()) {
                context_->usbDrive->checkIfReady();
            }
            loadDirectory();
            requestRedraw();
            return ScreenResult::stay();
        } else if (strcmp(actionLabel, "Play song") == 0) {
            return playFile(itemIndex);
        } else if (strcmp(actionLabel, "Add to queue") == 0) {
            // Only available for SD card source
            if (sourceType_ == SOURCE_SD) {
                if (file.isDirectory) {
                    return addFolderToQueue(itemIndex);
                } else {
                    return addFileToQueue(itemIndex);
                }
            }
            return ScreenResult::stay();
        } else if (strcmp(actionLabel, "Add to playlist") == 0) {
            // FUTURE: Implement playlist functionality (feature enhancement)
            return ScreenResult::stay();
        } else if (strcmp(actionLabel, "File info") == 0) {
            // FUTURE: Show file info dialog (feature enhancement)
            return ScreenResult::stay();
        } else if (strcmp(actionLabel, "Move to SD") == 0) {
            // FUTURE: Implement file transfer to SD (feature enhancement)
            return ScreenResult::stay();
        }

        return ScreenResult::stay();
    }

    // ============================================
    // DISPLAY METHODS
    // ============================================

    void drawHeader() override {
        // Draw window frame (DOS style)
        const char* title = getSourceTitle();
        context_->ui->drawWindow(0, 0, 100, 30, title, DOS_WHITE, DOS_BLUE);

        // Draw current path
        char pathDisplay[90];
        snprintf(pathDisplay, sizeof(pathDisplay), "Path: %.85s", currentPath_.c_str());
        context_->ui->drawText(2, 2, pathDisplay, DOS_YELLOW, DOS_BLUE);

        // Draw column headers
        context_->ui->drawText(4, 3, "Name", DOS_BRIGHT_CYAN, DOS_BLUE);
        context_->ui->drawText(45, 3, "Type", DOS_BRIGHT_CYAN, DOS_BLUE);
        context_->ui->drawText(55, 3, "Size", DOS_BRIGHT_CYAN, DOS_BLUE);

        // Separator line (DOS style)
        context_->ui->drawHLine(2, 4, 96, DOS_WHITE);
    }

    void drawFooter() override {
        // Status bar (DOS style)
        context_->ui->drawHLine(0, 28, 100, DOS_WHITE);

        // Global status bar (shows "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->draw();
        }

        // Show loading status as notification (right side) if needed
        if (loadingFloppyFiles_) {
            context_->ui->showStatusNotification("Reading disk...", 0, DOS_BLACK, DOS_YELLOW);
        }
    }

    void updateLCD() override {
        if (!context_->lcdManager) return;

        if (files_.size() == 0) {
            context_->lcdManager->setLine(0, "Empty folder");
            context_->lcdManager->setLine(1, "Sel:Back");
            return;
        }

        if (selectedIndex_ < 0 || selectedIndex_ >= (int)files_.size()) {
            return;
        }

        // Get current action
        const ItemAction* currentAction = getCurrentAction();
        if (!currentAction) {
            return;
        }

        // Line 1: Action description
        context_->lcdManager->setLine(0, currentAction->description);

        // Line 2: Simple button legend (avoid complex snprintf that causes issues)
        const FileEntry& file = files_[selectedIndex_];

        if (file.isDirectory || file.type == "BACK") {
            // Folder or Back: Simple navigation
            context_->lcdManager->setLine(1, "Sel:Open");
        } else {
            // File: Show current action with arrows and dot
            char line2[17];
            snprintf(line2, sizeof(line2), "%c%c %c%s",
                     LCD_CHAR_LEFT_ARROW, LCD_CHAR_RIGHT_ARROW,
                     LCD_CHAR_SELECT, currentAction->label);
            context_->lcdManager->setLine(1, line2);
        }
    }

private:
    // ============================================
    // HELPER METHODS
    // ============================================

    const char* getSourceTitle() const {
        switch (sourceType_) {
            case SOURCE_SD: return " SD Card Browser ";
            case SOURCE_USB: return " USB Drive Browser ";
            case SOURCE_FLOPPY: return " Floppy Drive Browser ";
            default: return " File Browser ";
        }
    }

    void loadDirectory() {
        files_.clear();
        selectedIndex_ = 0;
        scrollOffset_ = 0;
        currentActionIndex_ = 0;

        // Always add "Back" item at the top
        addBackItem();

        if (sourceType_ == SOURCE_SD) {
            loadSDDirectory();
        } else if (sourceType_ == SOURCE_USB) {
            loadUSBDirectory();
        } else if (sourceType_ == SOURCE_FLOPPY) {
            loadFloppyDirectory();
        }

    }

    void addBackItem() {
        FileEntry backItem;
        if (currentPath_ == "/") {
            backItem.name = "<< Back to Main Menu";
        } else {
            // Show parent folder name
            int lastSlash = currentPath_.lastIndexOf('/');
            String parentName = (lastSlash > 0) ? currentPath_.substring(0, lastSlash) : "/";
            if (parentName == "/") {
                backItem.name = "<< Back to Root";
            } else {
                int prevSlash = parentName.lastIndexOf('/');
                String folderName = (prevSlash >= 0) ? parentName.substring(prevSlash + 1) : parentName;
                backItem.name = "<< Back to " + folderName;
            }
        }
        backItem.isDirectory = true;
        backItem.size = 0;
        backItem.type = "BACK";
        files_.push_back(backItem);
    }

    void loadSDDirectory() {
        // CRITICAL: Disable Audio Library ISR during SD card access
        // The Audio Library and SD library both use SPI
        // If Audio ISR fires during SD read, SPI bus gets corrupted = LOCKUP
        AudioNoInterrupts();

        File dir = SD.open(currentPath_.c_str());

        if (dir && dir.isDirectory()) {
            File entry;
            while (entry = dir.openNextFile()) {
                addFileEntry(entry);
                entry.close();
            }
            dir.close();
            sortFiles();
        }

        // Re-enable Audio Library ISR
        AudioInterrupts();

        // Update static cache for SD card
        sdCache_.files = files_;
        sdCache_.path = currentPath_;
        sdCache_.valid = true;
        Serial.printf("[FileBrowser] SD cache updated (path=%s, %d files)\n",
                     currentPath_.c_str(), (int)files_.size());
    }

    void loadUSBDirectory() {
        if (!context_->hasUSBDrive()) {
            addErrorMessage("USB not available");
            return;
        }

        // Check cached drive status (updated by update() in main loop)
        // This is fast - no need for the slow checkIfReady() anymore
        if (!context_->usbDrive->isDriveReady()) {
            addErrorMessage("No USB drive - plug in and wait");
            return;
        }

        FS* usbFS = context_->usbDrive->getFilesystem();
        if (!usbFS) {
            addErrorMessage("USB error - try Refresh");
            return;
        }

        File dir = usbFS->open(currentPath_.c_str());
        if (dir && dir.isDirectory()) {
            File entry;
            while (entry = dir.openNextFile()) {
                addFileEntry(entry);
                entry.close();
            }
            dir.close();
            sortFiles();
        } else {
            addErrorMessage("Cannot open USB directory");
        }
    }

    void addErrorMessage(const char* message) {
        FileEntry errorItem;
        errorItem.name = message;
        errorItem.isDirectory = false;
        errorItem.size = 0;
        errorItem.type = "ERROR";
        files_.push_back(errorItem);
    }

    void loadFloppyDirectory() {
        if (!context_->hasFloppy() || !context_->floppy->isFloppyConnected()) {
            return;
        }

        // Check if we already have the file list cached
        const auto& cachedList = context_->floppy->getFileList();
        if (!cachedList.empty()) {
            loadFloppyFiles();
            return;
        }

        // Check disk status
        if (!context_->floppy->checkDiskStatus() || !context_->floppy->isDiskReady()) {
            return;
        }

        // Request file list (async operation)
        if (!context_->floppy->requestFileList()) {
            return;
        }

        // Show status notification
        context_->ui->showStatusNotification("Reading floppy disk...", 0, DOS_BLACK, DOS_YELLOW);

        // Mark as loading - files will be populated in update()
        loadingFloppyFiles_ = true;
    }

    void loadFloppyFiles() {
        if (!context_->hasFloppy()) return;

        const auto& floppyFiles = context_->floppy->getFileList();
        for (const auto& floppyFile : floppyFiles) {
            if (isSupportedFile(floppyFile.name.c_str())) {
                FileEntry fe;
                fe.name = floppyFile.name;
                fe.isDirectory = floppyFile.isDir;
                fe.size = 0;  // Floppy doesn't provide size
                fe.type = getFileType(floppyFile.name.c_str());
                files_.push_back(fe);
            }
        }

        sortFiles();
    }

    void addFileEntry(File& entry) {
        String name = entry.name();

        // Remove path prefix to get just filename
        int lastSlash = name.lastIndexOf('/');
        if (lastSlash >= 0) {
            name = name.substring(lastSlash + 1);
        }

        // Skip hidden files
        if (name[0] == '.') {
            return;
        }

        bool isDir = entry.isDirectory();

        // Check if it's a supported file
        if (isDir || isSupportedFile(name.c_str())) {
            FileEntry fe;
            fe.name = name;
            fe.isDirectory = isDir;
            fe.size = isDir ? 0 : entry.size();
            fe.type = isDir ? "" : getFileType(name.c_str());
            files_.push_back(fe);
        }
    }

    bool isSupportedFile(const char* filename) const {
        String lower = filename;
        lower.toLowerCase();
        return lower.endsWith(".mid") || lower.endsWith(".midi") ||
               lower.endsWith(".smf") || lower.endsWith(".kar") ||
               lower.endsWith(".vgm") || lower.endsWith(".vgz") ||
               lower.endsWith(".fm9") ||
               lower.endsWith(".spc") ||
               lower.endsWith(".mod") || lower.endsWith(".xm") ||
               lower.endsWith(".s3m") || lower.endsWith(".it");
    }

    String getFileType(const char* filename) const {
        String lower = filename;
        lower.toLowerCase();

        if (lower.endsWith(".mid") || lower.endsWith(".midi") ||
            lower.endsWith(".smf") || lower.endsWith(".kar")) {
            return "MIDI";
        } else if (lower.endsWith(".vgm") || lower.endsWith(".vgz")) {
            return "VGM";
        } else if (lower.endsWith(".fm9")) {
            return "FM9";
        } else if (lower.endsWith(".spc")) {
            return "SPC";
        } else if (lower.endsWith(".mod")) {
            return "MOD";
        } else if (lower.endsWith(".xm")) {
            return "XM";
        } else if (lower.endsWith(".s3m")) {
            return "S3M";
        } else if (lower.endsWith(".it")) {
            return "IT";
        }
        return "?";
    }

    void formatFileSize(uint32_t size, char* buffer, size_t bufferSize) const {
        if (size < 1024) {
            snprintf(buffer, bufferSize, "%dB", size);
        } else if (size < 1024 * 1024) {
            snprintf(buffer, bufferSize, "%dK", size / 1024);
        } else {
            snprintf(buffer, bufferSize, "%dM", size / (1024 * 1024));
        }
    }

    void sortFiles() {
        // Sort: Back item first, then directories, then files alphabetically
        std::sort(files_.begin() + 1, files_.end(),
            [](const FileEntry& a, const FileEntry& b) -> bool {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory;  // Directories first
                }
                return a.name < b.name;  // Alphabetical
            });
    }

    ScreenResult openFolder(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            return ScreenResult::stay();
        }

        const FileEntry& file = files_[itemIndex];
        if (!file.isDirectory) {
            return ScreenResult::stay();
        }

        // Update current path
        if (currentPath_ == "/") {
            currentPath_ = "/" + file.name;
        } else {
            currentPath_ += "/" + file.name;
        }

        // Reload directory
        loadDirectory();
        requestRedraw();

        return ScreenResult::stay();
    }

    ScreenResult goBack() {
        if (currentPath_ == "/") {
            // At root - go back to main menu
            return ScreenResult::goBack();
        }

        // Go to parent directory
        int lastSlash = currentPath_.lastIndexOf('/');
        if (lastSlash <= 0) {
            currentPath_ = "/";
        } else {
            currentPath_ = currentPath_.substring(0, lastSlash);
        }

        // Reload directory
        loadDirectory();
        requestRedraw();

        return ScreenResult::stay();
    }

    ScreenResult playFile(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            return ScreenResult::stay();
        }

        const FileEntry& file = files_[itemIndex];
        if (file.isDirectory) {
            return ScreenResult::stay();
        }

        String fullPath;

        // For floppy, transfer file first
        if (sourceType_ == SOURCE_FLOPPY) {
            if (!context_->hasFloppy() || !context_->floppy->isFloppyConnected()) {
                context_->ui->showStatusNotification("Floppy not connected!", 3000, DOS_WHITE, DOS_RED);
                return ScreenResult::stay();
            }

            // Show a message before transfer (transfer is blocking, so we can't animate)
            context_->ui->showStatusNotification("Transferring file...", 0, DOS_BLACK, DOS_YELLOW);

            // Transfer file from floppy to temp (BLOCKING operation)
            // Note: During this call, the UI will not update. This is expected behavior
            // for hardware file transfers. The status notification will remain visible.
            fullPath = context_->floppy->transferFile(file.name.c_str());

            if (fullPath.length() == 0) {
                context_->ui->showStatusNotification("Transfer failed!", 3000, DOS_WHITE, DOS_RED);
                requestRedraw();  // Redraw the file list
                return ScreenResult::stay();
            }

            // Transfer succeeded - Set FileSource to SD (temp files are on SD)
            context_->fileSource->setSource(FileSource::SD_CARD);
        } else {
            // Set FileSource based on source type
            if (sourceType_ == SOURCE_USB && context_->hasUSBDrive() && context_->usbDrive->isDriveReady()) {
                FS* usbFS = context_->usbDrive->getFilesystem();
                context_->fileSource->setSource(FileSource::USB_DRIVE, (void*)usbFS);
            } else {
                context_->fileSource->setSource(FileSource::SD_CARD);
            }

            // Build full path
            if (currentPath_ == "/") {
                fullPath = "/" + file.name;
            } else {
                fullPath = currentPath_ + "/" + file.name;
            }
        }

        // Notify handler that user wants to see Now Playing screen
        PlaybackNavigationHandler::notifyUserWantsNowPlaying();

        // Request play through coordinator
        // Coordinator handles:
        // - Stopping current player (if any)
        // - Loading file asynchronously
        // - Waiting for screen to finish drawing
        // - Starting playback
        // - Navigation (via PlaybackNavigationHandler)
        // - Error handling
        context_->coordinator->requestPlay(fullPath.c_str());
        return ScreenResult::stay();  // Navigation handler will decide where to go
    }

    ScreenResult addFileToQueue(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            return ScreenResult::stay();
        }

        const FileEntry& file = files_[itemIndex];
        if (file.isDirectory) {
            return ScreenResult::stay();
        }

        // Build full path
        String fullPath;
        if (currentPath_ == "/") {
            fullPath = "/" + file.name;
        } else {
            fullPath = currentPath_ + "/" + file.name;
        }

        // Add to queue via EventManager (proper framework usage)
        if (context_->queueManager) {
            context_->queueManager->addToQueue(fullPath.c_str());

            // Show notification
            char msg[64];
            snprintf(msg, sizeof(msg), "Added: %.40s", file.name.c_str());
            context_->ui->showStatusNotification(msg, 2000, DOS_BLACK, DOS_LIGHT_GRAY);
        }

        return ScreenResult::stay();
    }

    ScreenResult addFolderToQueue(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= (int)files_.size()) {
            return ScreenResult::stay();
        }

        const FileEntry& folder = files_[itemIndex];
        if (!folder.isDirectory) {
            return ScreenResult::stay();
        }

        // Build full folder path
        String folderPath;
        if (currentPath_ == "/") {
            folderPath = "/" + folder.name;
        } else {
            folderPath = currentPath_ + "/" + folder.name;
        }

        // CRITICAL: Disable Audio Library ISR during SD card access
        AudioNoInterrupts();

        // Scan folder and add all supported files
        int addedCount = 0;
        File dir = SD.open(folderPath.c_str());

        if (dir && dir.isDirectory()) {
            File entry;
            while (entry = dir.openNextFile()) {
                // Skip directories (non-recursive for v1)
                if (entry.isDirectory()) {
                    entry.close();
                    continue;
                }

                String fileName = entry.name();
                // Extract just filename (no path)
                int lastSlash = fileName.lastIndexOf('/');
                if (lastSlash >= 0) {
                    fileName = fileName.substring(lastSlash + 1);
                }

                // Check if supported file
                if (isSupportedFile(fileName.c_str())) {
                    // Build full path
                    String filePath = folderPath + "/" + fileName;

                    // Add to queue
                    if (context_->queueManager) {
                        context_->queueManager->addToQueue(filePath.c_str());
                        addedCount++;
                    }
                }

                entry.close();
            }
            dir.close();
        }

        // Re-enable Audio Library ISR
        AudioInterrupts();

        // Show notification
        if (addedCount > 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Added %d files from %s", addedCount, folder.name.c_str());
            context_->ui->showStatusNotification(msg, 3000, DOS_BLACK, DOS_LIGHT_GRAY);
        } else {
            context_->ui->showStatusNotification("No music files in folder", 2000, DOS_WHITE, DOS_RED);
        }

        return ScreenResult::stay();
    }

    // ============================================
    // EVENT CALLBACKS
    // ============================================

    static void onUSBConnected(void* ctx) {
        auto* screen = (FileBrowserScreenNew*)ctx;
        screen->loadDirectory();
        screen->requestRedraw();
    }

    static void onUSBDisconnected(void* ctx) {
        auto* screen = (FileBrowserScreenNew*)ctx;
        screen->loadDirectory();
        screen->requestRedraw();
    }
};

// Static action definitions
ActionableListScreenBase::ItemAction FileBrowserScreenNew::folderActions_[2] = {
    {"Open folder", "Open this folder"},
    {"Go back", "Return to parent"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::sdFolderActions_[3] = {
    {"Open folder", "Open this folder"},
    {"Add to queue", "Queue all files"},
    {"Go back", "Return to parent"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::fileActions_[4] = {
    {"Play song", "Play this file"},
    {"Add to queue", "Queue for later"},
    {"Add to playlist", "Save to playlist"},
    {"File info", "View file details"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::usbFileActions_[3] = {
    {"Play song", "Play this file"},
    {"Move to SD", "Copy to SD card"},
    {"Refresh", "Check for USB drive"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::usbBackActions_[2] = {
    {"Go back", "Return to menu"},
    {"Refresh", "Check for USB drive"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::floppyFileActions_[3] = {
    {"Play song", "Play this file"},
    {"Move to SD", "Copy to SD card"},
    {"Refresh", "Reload file list"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::backAction_[1] = {
    {"Go back", "Return to menu"}
};

ActionableListScreenBase::ItemAction FileBrowserScreenNew::floppyBackActions_[2] = {
    {"Go back", "Return to menu"},
    {"Refresh", "Reload file list"}
};

// Initialize static SD cache
FileBrowserScreenNew::SDDirectoryCache FileBrowserScreenNew::sdCache_ = {
    std::vector<FileBrowserScreenNew::FileEntry>(),  // files
    "",                                               // path
    false                                             // valid
};

#endif // FILE_BROWSER_SCREEN_NEW_H
