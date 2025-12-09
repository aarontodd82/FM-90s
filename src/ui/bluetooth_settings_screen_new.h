#ifndef BLUETOOTH_SETTINGS_SCREEN_NEW_H
#define BLUETOOTH_SETTINGS_SCREEN_NEW_H

#include "framework/actionable_list_screen_base.h"
#include "framework/info_panel.h"
#include "framework/progress_indicator.h"
#include "framework/loading_overlay.h"
#include "framework/modal_dialog.h"
#include "framework/status_bar_manager.h"
#include "../bluetooth_async_operations.h"
#include "../bluetooth_manager.h"
#include "../dos_colors.h"
#include "screen_id.h"

/**
 * BluetoothSettingsScreenNew - Bluetooth management using framework best practices
 *
 * Properly uses ActionableListScreenBase for list/menu management
 * Framework handles: navigation, selection, scrolling, LCD updates
 * We provide: item data, actions, and state-specific logic
 */

class BluetoothSettingsScreenNew : public ActionableListScreenBase {
private:
    // Internal state (not StatefulScreen - simpler state management)
    enum State {
        STATE_INITIALIZING,      // Querying ESP32 status
        STATE_NOT_CONNECTED,     // Ready to scan
        STATE_SCANNING,          // Scanning in progress
        STATE_SCAN_RESULTS,      // Devices found, select to connect
        STATE_CONNECTING,        // Connection in progress
        STATE_CONNECTED,         // Connected to device
        STATE_WAITING_AUTO       // Auto-reconnect enabled, waiting
    };
    State currentState_;

    // Async operations
    BluetoothScanOperation* scanOp_;
    BluetoothConnectOperation* connectOp_;
    BluetoothDisconnectOperation* disconnectOp_;

    // UI Components
    InfoPanel* infoPanel_;
    ProgressIndicator* scanProgress_;
    LoadingOverlay* loadingOverlay_;

    // Initialization tracking
    unsigned long queryStartTime_;
    static const unsigned long QUERY_TIMEOUT = 2000;  // 2 seconds

    // Progress update throttling
    unsigned long lastProgressUpdate_;
    static const unsigned long PROGRESS_UPDATE_INTERVAL = 1000;  // Update every 1 second

    // Item data (dynamically built based on state)
    struct MenuItem {
        char label[64];
        int actionID;
    };
    std::vector<MenuItem> items_;

    // Action IDs
    enum ActionID {
        ACTION_NONE = -1,
        ACTION_SCAN = 0,
        ACTION_STOP_SCAN = 1,
        ACTION_RESCAN = 2,
        ACTION_CONNECT_DEVICE = 10,  // 10+ = device index
        ACTION_DISCONNECT = 100,
        ACTION_ENABLE_AUTO = 101,
        ACTION_DISABLE_AUTO = 102,
        ACTION_BACK = 200
    };

    // Layout
    static const int INFO_PANEL_ROW = 3;
    static const int INFO_PANEL_HEIGHT = 3;
    static const int MENU_START_ROW = 7;

public:
    BluetoothSettingsScreenNew(ScreenContext* context)
        : ActionableListScreenBase(context, 20, MENU_START_ROW, 1),
          currentState_(STATE_INITIALIZING),
          scanOp_(nullptr),
          connectOp_(nullptr),
          disconnectOp_(nullptr),
          infoPanel_(nullptr),
          scanProgress_(nullptr),
          loadingOverlay_(nullptr),
          queryStartTime_(0),
          lastProgressUpdate_(0) {}

    virtual ~BluetoothSettingsScreenNew() {
        cleanup();
    }

    // ============================================
    // LIFECYCLE
    // ============================================

    void onCreate(void* params) override {
        // Create UI components
        infoPanel_ = new InfoPanel(context_->ui, 6, INFO_PANEL_ROW, 68, INFO_PANEL_HEIGHT);
        scanProgress_ = new ProgressIndicator(context_->ui, 6, INFO_PANEL_ROW + 2, 68);
        loadingOverlay_ = new LoadingOverlay(context_->ui);

        // Register for Bluetooth events
        if (context_->eventManager && context_->hasBluetooth()) {
            context_->eventManager->on(EventManager::EVENT_BT_CONNECTED, onBTConnected, this);
            context_->eventManager->on(EventManager::EVENT_BT_DISCONNECTED, onBTDisconnected, this);
            context_->eventManager->on(EventManager::EVENT_BT_SCAN_COMPLETE, onScanComplete, this);
        }

        // // Serial.println("[BluetoothSettings] Created");
    }

    void onEnter() override {
        // Query ESP32 for current status
        if (!context_->hasBluetooth()) {
            // // Serial.println("[BluetoothSettings] Bluetooth not available");
            changeState(STATE_NOT_CONNECTED);
        } else {
            // // Serial.println("[BluetoothSettings] Querying ESP32 status...");
            changeState(STATE_INITIALIZING);

            // Show loading overlay immediately (before drawing)
            if (loadingOverlay_) {
                loadingOverlay_->show("Querying Bluetooth status...");
            }

            // Query current state
            context_->bluetooth->queryStatus();
            context_->bluetooth->queryAutoReconnectStatus();
            context_->bluetooth->queryAutoConnectDevice();
            queryStartTime_ = millis();
        }

        // Call base class (this will draw, but overlay will be on top)
        ActionableListScreenBase::onEnter();
    }

    void onDestroy() override {
        cleanup();

        // Unregister events
        if (context_->eventManager) {
            context_->eventManager->offAll(this);
        }
    }

    void update() override {
        // Update global status bar (dynamic "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->update();
        }

        // Handle INITIALIZING state timeout
        if (currentState_ == STATE_INITIALIZING) {
            if ((millis() - queryStartTime_) > QUERY_TIMEOUT) {
                // // Serial.println("[BluetoothSettings] Query timeout, determining state");
                determineStateFromManager();
            }
        }

        // Update async operations
        if (scanOp_ && scanOp_->isRunning()) {
            scanOp_->update();

            // Redraw progress indicator once per second (not every frame - too blinky)
            if (currentState_ == STATE_SCANNING && scanProgress_) {
                unsigned long now = millis();
                if (now - lastProgressUpdate_ >= PROGRESS_UPDATE_INTERVAL) {
                    scanProgress_->draw();
                    lastProgressUpdate_ = now;
                }
            }

            if (scanOp_->isDone()) {
                handleScanComplete();
            }
        }

        if (connectOp_ && connectOp_->isRunning()) {
            connectOp_->update();
            if (connectOp_->isDone()) {
                handleConnectComplete();
            }
        }

        if (disconnectOp_ && disconnectOp_->isRunning()) {
            disconnectOp_->update();
            if (disconnectOp_->isDone()) {
                handleDisconnectComplete();
            }
        }

        // Update loading overlay animation
        if (loadingOverlay_ && loadingOverlay_->isVisible()) {
            loadingOverlay_->update();
        }

        // Call base class
        ActionableListScreenBase::update();
    }

    // ============================================
    // ActionableListScreenBase IMPLEMENTATION
    // ============================================

    int getItemCount() override {
        return items_.size();
    }

    void drawItem(int itemIndex, int row, bool selected) override {
        if (itemIndex < 0 || itemIndex >= (int)items_.size()) return;

        const MenuItem& item = items_[itemIndex];
        int actionID = item.actionID;

        uint16_t fg = selected ? DOS_BLACK : DOS_WHITE;
        uint16_t bg = selected ? DOS_CYAN : DOS_BLUE;

        // Color coding for special items
        if (actionID == ACTION_DISCONNECT) {
            fg = selected ? DOS_BLACK : DOS_RED;
        } else if (actionID == ACTION_ENABLE_AUTO || actionID == ACTION_DISABLE_AUTO) {
            fg = selected ? DOS_BLACK : DOS_YELLOW;
        } else if (actionID >= ACTION_CONNECT_DEVICE && actionID < ACTION_DISCONNECT) {
            fg = selected ? DOS_BLACK : DOS_GREEN;
        }

        context_->ui->fillGridRect(4, row, 72, 1, bg);

        if (selected) {
            context_->ui->drawText(4, row, "\x10", DOS_BLACK, DOS_CYAN);
        }

        context_->ui->drawText(6, row, item.label, fg, bg);
    }

    const ItemAction* getItemActions(int itemIndex, int& count) override {
        // Simple actions: just SELECT to execute
        static ItemAction actions[] = {
            {"Select", "Execute action"}
        };
        count = 1;
        return actions;
    }

    ScreenResult onActionExecuted(int itemIndex, int actionIndex) override {
        if (itemIndex < 0 || itemIndex >= (int)items_.size()) {
            return ScreenResult::stay();
        }

        int actionID = items_[itemIndex].actionID;

        // // Serial.print("[BluetoothSettings] Action: ");
        // // Serial.println(actionID);

        switch (actionID) {
            case ACTION_SCAN:
                changeState(STATE_SCANNING);
                break;

            case ACTION_STOP_SCAN:
                if (scanOp_) scanOp_->cancel();
                changeState(STATE_NOT_CONNECTED);
                break;

            case ACTION_RESCAN:
                changeState(STATE_SCANNING);
                break;

            case ACTION_DISCONNECT:
                startDisconnect();
                break;

            case ACTION_ENABLE_AUTO:
                // // Serial.println("[BluetoothSettings] Enabling auto-reconnect");
                context_->bluetooth->setAutoReconnect(true);
                // Rebuild menu immediately to show change
                delay(100);  // Brief delay for command to process
                buildItemsForState();
                requestRedraw();
                // Will transition to WAITING_AUTO via event if not already connected
                break;

            case ACTION_DISABLE_AUTO:
                // // Serial.println("[BluetoothSettings] Disabling auto-reconnect");
                context_->bluetooth->setAutoReconnect(false);
                delay(100);  // Brief delay for command to process
                // Rebuild menu to show change
                buildItemsForState();
                requestRedraw();
                // Stay in current state (might be CONNECTED or WAITING_AUTO)
                break;

            case ACTION_BACK:
                return ScreenResult::goBack();

            default:
                // Check if it's a device connection
                if (actionID >= ACTION_CONNECT_DEVICE && actionID < ACTION_DISCONNECT) {
                    int deviceIndex = actionID - ACTION_CONNECT_DEVICE;
                    startConnect(deviceIndex);
                }
                break;
        }

        return ScreenResult::stay();
    }

    void drawHeader() override {
        context_->ui->drawWindow(0, 0, 100, 30, " BLUETOOTH SETTINGS ", DOS_WHITE, DOS_BLUE);

        // Draw info panel
        if (infoPanel_) {
            infoPanel_->draw();
        }

        // Draw scan progress if scanning
        if (currentState_ == STATE_SCANNING && scanProgress_) {
            scanProgress_->draw();
        }

        // Draw loading overlay if visible
        if (loadingOverlay_ && loadingOverlay_->isVisible()) {
            loadingOverlay_->update();
        }
    }

    void drawFooter() override {
        context_->ui->drawHLine(0, 28, 100, DOS_WHITE);

        // Global status bar (shows "Now:" and "Next:")
        if (context_->statusBarManager) {
            context_->statusBarManager->draw();
        }

        // Show Bluetooth state as notification (right side)
        const char* stateStr = getStateName(currentState_);
        char status[32];
        snprintf(status, sizeof(status), "BT: %s", stateStr);
        context_->ui->showStatusNotification(status, 0, DOS_BLACK, DOS_LIGHT_GRAY);
    }

private:
    // ============================================
    // STATE MANAGEMENT
    // ============================================

    void changeState(State newState) {
        if (newState == currentState_) return;

        // // Serial.print("[BluetoothSettings] State: ");
        // // Serial.print(getStateName(currentState_));
        // // Serial.print(" -> ");
        // // Serial.println(getStateName(newState));

        currentState_ = newState;
        buildItemsForState();
        requestRedraw();
    }

    const char* getStateName(State state) const {
        switch (state) {
            case STATE_INITIALIZING: return "INITIALIZING";
            case STATE_NOT_CONNECTED: return "NOT_CONNECTED";
            case STATE_SCANNING: return "SCANNING";
            case STATE_SCAN_RESULTS: return "SCAN_RESULTS";
            case STATE_CONNECTING: return "CONNECTING";
            case STATE_CONNECTED: return "CONNECTED";
            case STATE_WAITING_AUTO: return "WAITING_AUTO";
            default: return "UNKNOWN";
        }
    }

    void determineStateFromManager() {
        // Called after querying ESP32 (or timeout)
        if (!context_->hasBluetooth()) {
            changeState(STATE_NOT_CONNECTED);
            return;
        }

        if (context_->bluetooth->isConnected()) {
            // // Serial.println("[BluetoothSettings] Already connected");
            changeState(STATE_CONNECTED);
        } else if (context_->bluetooth->getAutoReconnect() &&
                   context_->bluetooth->hasAutoConnectDevice()) {
            // // Serial.println("[BluetoothSettings] Auto-reconnect active");
            changeState(STATE_WAITING_AUTO);
        } else {
            // // Serial.println("[BluetoothSettings] Not connected");
            changeState(STATE_NOT_CONNECTED);
        }
    }

    // ============================================
    // ITEM BUILDING (STATE-SPECIFIC)
    // ============================================

    void buildItemsForState() {
        items_.clear();

        switch (currentState_) {
            case STATE_INITIALIZING:
                if (infoPanel_) infoPanel_->showStatus("Checking Bluetooth status...");
                // Loading overlay shown in onEnter, don't rebuild here
                // Add empty item so list doesn't show "no items"
                addItem("Initializing...", ACTION_NONE);
                break;

            case STATE_NOT_CONNECTED:
                if (loadingOverlay_ && loadingOverlay_->isVisible()) loadingOverlay_->hide();
                if (infoPanel_) infoPanel_->showReminder("Put Bluetooth device in pairing mode before scanning");
                addItem("Scan for devices", ACTION_SCAN);
                addItem("Back to settings", ACTION_BACK);
                break;

            case STATE_SCANNING:
                if (infoPanel_) infoPanel_->showStatus("Scanning for Bluetooth devices...");
                startScanOperation();
                addItem("Stop scan", ACTION_STOP_SCAN);
                break;

            case STATE_SCAN_RESULTS:
                {
                    int count = context_->bluetooth->getDeviceCount();
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Found %d device%s - Select to connect",
                             count, count != 1 ? "s" : "");
                    if (infoPanel_) infoPanel_->showStatus(msg);

                    // Add devices
                    for (int i = 0; i < count; i++) {
                        const auto* device = context_->bluetooth->getDevice(i);
                        if (device) {
                            char label[64];
                            snprintf(label, sizeof(label), "%s (RSSI: %d)", device->name, device->rssi);
                            addItem(label, ACTION_CONNECT_DEVICE + i);
                        }
                    }

                    addItem("Rescan", ACTION_RESCAN);
                    addItem("Back to settings", ACTION_BACK);
                }
                break;

            case STATE_CONNECTING:
                if (loadingOverlay_) loadingOverlay_->show("Connecting to Bluetooth device...");
                if (infoPanel_) infoPanel_->showStatus("Connecting...");
                addItem("Connecting...", ACTION_NONE);
                break;

            case STATE_CONNECTED:
                {
                    if (loadingOverlay_ && loadingOverlay_->isVisible()) loadingOverlay_->hide();

                    char msg[64];
                    const char* deviceName = context_->bluetooth->getConnectedDeviceName();

                    // Check if device name is available
                    if (deviceName && strlen(deviceName) > 0) {
                        snprintf(msg, sizeof(msg), "Connected to: %s", deviceName);
                        if (infoPanel_) infoPanel_->showStatus(msg);

                        char connLabel[64];
                        snprintf(connLabel, sizeof(connLabel), "Connected: %s", deviceName);
                        addItem(connLabel, ACTION_NONE);
                    } else {
                        // Device name not yet received, show generic message
                        strcpy(msg, "Connected (retrieving device info...)");
                        if (infoPanel_) infoPanel_->showStatus(msg);
                        addItem("Connected (retrieving info...)", ACTION_NONE);
                    }

                    addItem("Disconnect", ACTION_DISCONNECT);

                    if (context_->bluetooth->getAutoReconnect()) {
                        addItem("Disable auto-reconnect", ACTION_DISABLE_AUTO);
                    } else {
                        addItem("Enable auto-reconnect", ACTION_ENABLE_AUTO);
                    }

                    addItem("Back to settings", ACTION_BACK);
                }
                break;

            case STATE_WAITING_AUTO:
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Auto-reconnecting to: %s",
                             context_->bluetooth->getAutoConnectDeviceName());
                    if (infoPanel_) infoPanel_->showStatus(msg);

                    addItem("Waiting for auto-connect...", ACTION_NONE);
                    addItem("Disable auto-reconnect", ACTION_DISABLE_AUTO);
                    addItem("Back to settings", ACTION_BACK);
                }
                break;
        }
    }

    void addItem(const char* label, int actionID) {
        MenuItem item;
        strncpy(item.label, label, sizeof(item.label) - 1);
        item.label[sizeof(item.label) - 1] = '\0';
        item.actionID = actionID;
        items_.push_back(item);
    }

    // ============================================
    // ASYNC OPERATIONS
    // ============================================

    void startScanOperation() {
        if (!context_->hasBluetooth()) return;
        if (scanOp_) return;  // Already scanning

        scanOp_ = new BluetoothScanOperation(context_->bluetooth, 25000);  // 25s timeout

        if (scanProgress_) {
            scanProgress_->setStyle(ProgressIndicator::STYLE_BAR_TIME);
            scanProgress_->setLabel("Scanning");
            scanOp_->attachProgressIndicator(scanProgress_);
        }

        scanOp_->start();
        // // Serial.println("[BluetoothSettings] Scan started");
    }

    void startConnect(int deviceIndex) {
        if (!context_->hasBluetooth()) return;

        const auto* device = context_->bluetooth->getDevice(deviceIndex);
        if (!device) {
            // // Serial.println("[BluetoothSettings] Invalid device index");
            return;
        }

        changeState(STATE_CONNECTING);
        connectOp_ = new BluetoothConnectOperation(context_->bluetooth, device->address, 15000);
        connectOp_->start();

        // // Serial.print("[BluetoothSettings] Connecting to: ");
        // // Serial.println(device->address);
    }

    void startDisconnect() {
        if (!context_->hasBluetooth()) return;

        disconnectOp_ = new BluetoothDisconnectOperation(context_->bluetooth, 5000);
        disconnectOp_->start();
        // // Serial.println("[BluetoothSettings] Disconnecting...");
    }

    void handleScanComplete() {
        if (!scanOp_) return;

        if (scanOp_->isSuccess()) {
            // // Serial.println("[BluetoothSettings] Scan complete");
            changeState(STATE_SCAN_RESULTS);
        } else {
            // // Serial.print("[BluetoothSettings] Scan failed: ");
            // // Serial.println(scanOp_->getErrorMessage());

            if (infoPanel_) {
                infoPanel_->showError("Scan failed");
            }
            changeState(STATE_NOT_CONNECTED);
        }

        delete scanOp_;
        scanOp_ = nullptr;
    }

    void handleConnectComplete() {
        if (!connectOp_) return;

        if (connectOp_->isSuccess()) {
            // // Serial.println("[BluetoothSettings] Connected");

            // Transition to connected state first
            changeState(STATE_CONNECTED);

            // Wait a moment for device name to arrive (DEVICE_NAME: notification)
            delay(200);

            // Ask if user wants to enable auto-reconnect
            // Modal blocks and handles its own event loop
            auto result = ModalDialog::showYesNo(
                context_->ui,
                context_->lcd,
                "Auto-Reconnect?",
                "Enable automatic reconnection to this device?\n\nUP/DOWN to select, SELECT to confirm."
            );

            if (result == ModalDialog::RESULT_YES) {
                // // Serial.println("[BluetoothSettings] User enabled auto-reconnect");
                context_->bluetooth->setAutoReconnect(true);
            } else {
                // // Serial.println("[BluetoothSettings] User declined auto-reconnect");
            }

            // Rebuild menu to show updated auto-reconnect state
            buildItemsForState();

            // Redraw screen after modal (modal doesn't restore screen)
            requestRedraw();
        } else {
            // // Serial.print("[BluetoothSettings] Connection failed: ");
            // // Serial.println(connectOp_->getErrorMessage());

            ModalDialog::showError(context_->ui, context_->lcd,
                                  connectOp_->getErrorMessage());

            // Redraw after modal
            requestRedraw();
            changeState(STATE_SCAN_RESULTS);
        }

        delete connectOp_;
        connectOp_ = nullptr;
    }

    void handleDisconnectComplete() {
        if (!disconnectOp_) return;

        if (disconnectOp_->isSuccess()) {
            // // Serial.println("[BluetoothSettings] Disconnected");
        } else {
            // // Serial.print("[BluetoothSettings] Disconnect failed: ");
            // // Serial.println(disconnectOp_->getErrorMessage());
        }

        changeState(STATE_NOT_CONNECTED);
        delete disconnectOp_;
        disconnectOp_ = nullptr;
    }

    void cleanup() {
        if (scanOp_) { delete scanOp_; scanOp_ = nullptr; }
        if (connectOp_) { delete connectOp_; connectOp_ = nullptr; }
        if (disconnectOp_) { delete disconnectOp_; disconnectOp_ = nullptr; }
        if (infoPanel_) { delete infoPanel_; infoPanel_ = nullptr; }
        if (scanProgress_) { delete scanProgress_; scanProgress_ = nullptr; }
        if (loadingOverlay_) { delete loadingOverlay_; loadingOverlay_ = nullptr; }
    }

    // ============================================
    // EVENT CALLBACKS
    // ============================================

    static void onBTConnected(void* ctx) {
        auto* screen = (BluetoothSettingsScreenNew*)ctx;
        // // Serial.println("[BluetoothSettings] EVENT: Connected");

        // If we're in INITIALIZING, determine state
        if (screen->currentState_ == STATE_INITIALIZING) {
            screen->determineStateFromManager();
        } else if (screen->currentState_ != STATE_CONNECTED) {
            screen->changeState(STATE_CONNECTED);
        } else {
            // Already in CONNECTED state, but device name might have just arrived
            // Rebuild menu to refresh device name display
            screen->buildItemsForState();
            screen->requestRedraw();
        }
    }

    static void onBTDisconnected(void* ctx) {
        auto* screen = (BluetoothSettingsScreenNew*)ctx;
        // // Serial.println("[BluetoothSettings] EVENT: Disconnected");

        // Check if auto-reconnect is enabled
        if (screen->context_->bluetooth->getAutoReconnect() &&
            screen->context_->bluetooth->hasAutoConnectDevice()) {
            screen->changeState(STATE_WAITING_AUTO);
        } else {
            screen->changeState(STATE_NOT_CONNECTED);
        }
    }

    static void onScanComplete(void* ctx) {
        auto* screen = (BluetoothSettingsScreenNew*)ctx;
        // // Serial.println("[BluetoothSettings] EVENT: Scan complete");

        // Mark the async operation as complete so progress indicator updates
        if (screen->scanOp_) {
            screen->scanOp_->markComplete();
        }
    }
};

#endif // BLUETOOTH_SETTINGS_SCREEN_NEW_H
