#include "bluetooth_manager.h"
#include "ui/framework/event_manager.h"
#include "debug_config.h"

BluetoothManager::BluetoothManager()
    : initialized_(false)
    , esp32Ready_(false)
    , connState_(NOT_CONNECTED)
    , currentOp_(IDLE)
    , eventMgr_(nullptr)
    , deviceCount_(0)
    , receivingDeviceList_(false)
    , tempDeviceCount_(0)
    , receivingInfo_(false)
    , autoReconnectEnabled_(false)
    , volume_(100)
    , hasError_(false)
    , lastCommandTime_(0) {

    memset(devices_, 0, sizeof(devices_));
    memset(connectedDeviceName_, 0, sizeof(connectedDeviceName_));
    memset(connectedDeviceAddress_, 0, sizeof(connectedDeviceAddress_));
    memset(autoConnectDeviceName_, 0, sizeof(autoConnectDeviceName_));
    memset(autoConnectDeviceAddress_, 0, sizeof(autoConnectDeviceAddress_));
    strcpy(statusMessage_, "Not initialized");
    errorMessage_[0] = '\0';
}

void BluetoothManager::begin() {
    // Serial3 should already be initialized in main.cpp
    // Clear any pending data
    while (Serial3.available()) {
        Serial3.read();
    }
    responseBuffer_ = "";
}

bool BluetoothManager::initialize() {
    // Send INIT command
    sendInit();

    // Wait for SYSTEM:READY response
    unsigned long initStart = millis();
    while (!initialized_ && (millis() - initStart < 10000)) {
        update();
        delay(10);
    }

    if (!initialized_) {
        // // Serial.println("[BT] ERROR: Bluetooth initialization failed");
        return false;
    }

    return true;
}

void BluetoothManager::update() {
    // Process incoming data from ESP32
    while (Serial3.available()) {
        char c = Serial3.read();

        if (c == '\n' || c == '\r') {
            if (responseBuffer_.length() > 0) {
                parseLine(responseBuffer_);
                responseBuffer_ = "";
            }
        } else {
            // Pre-allocate buffer capacity to avoid repeated memory reallocations
            if (responseBuffer_.length() == 0) {
                responseBuffer_.reserve(128);
            }
            responseBuffer_ += c;
        }
    }

    // Check for command timeout
    if (currentOp_ != IDLE && (millis() - lastCommandTime_) > COMMAND_TIMEOUT) {
        strcpy(errorMessage_, "Command timeout");
        hasError_ = true;
        currentOp_ = IDLE;
    }
}

void BluetoothManager::sendCommand(const char* cmd) {
    #if DEBUG_BLUETOOTH
    // // Serial.print("[BT TX] ");
    // // Serial.println(cmd);
    #endif
    Serial3.println(cmd);
    lastCommandTime_ = millis();
    hasError_ = false;
}

void BluetoothManager::parseLine(const String& line) {
    String trimmed = line;
    trimmed.trim();

    if (trimmed.length() == 0) return;

    // Debug output - show everything received from ESP32
    #if DEBUG_BLUETOOTH
    // // Serial.print("[BT RX] ");
    // // Serial.println(trimmed);
    #endif

    // Parse responses based on protocol
    if (trimmed.startsWith("ESP32:READY")) {
        handleSystemReady();
    }
    else if (trimmed.startsWith("SYSTEM:READY")) {
        handleInitialized();
    }
    else if (trimmed.startsWith("I2S:INITIALIZED") || trimmed.startsWith("BT:INITIALIZED")) {
        // Part of initialization sequence
    }
    else if (trimmed.startsWith("SCAN:STARTED")) {
        handleScanStarted();
    }
    else if (trimmed.startsWith("SCAN:COMPLETE")) {
        handleScanComplete();
    }
    else if (trimmed.startsWith("SCAN:STOPPED")) {
        currentOp_ = IDLE;
        strcpy(statusMessage_, "Scan stopped");
    }
    else if (trimmed.startsWith("DEVICES_FOUND:")) {
        int count = trimmed.substring(14).toInt();
        handleDevicesFound(count);
    }
    else if (trimmed.startsWith("INFO:START")) {
        receivingInfo_ = true;
    }
    else if (trimmed.startsWith("INFO:END")) {
        receivingInfo_ = false;
    }
    else if (receivingInfo_) {
        handleInfoLine(trimmed);
    }
    else if (trimmed.startsWith("DEVICES:START")) {
        handleDeviceList(trimmed);
    }
    else if (trimmed.startsWith("DEVICE:")) {
        int oldCount = tempDeviceCount_;
        handleDevice(trimmed.substring(7));

        // Fire event if a new device was added
        if (tempDeviceCount_ > oldCount && eventMgr_) {
            eventMgr_->fireInt(EventManager::EVENT_BT_DEVICE_FOUND, tempDeviceCount_ - 1);
        }
    }
    else if (trimmed.startsWith("COUNT:")) {
        deviceCount_ = trimmed.substring(6).toInt();
    }
    else if (trimmed.startsWith("DEVICES:END")) {
        receivingDeviceList_ = false;
        currentOp_ = IDLE;
        sprintf(statusMessage_, "Found %d device%s", deviceCount_, deviceCount_ == 1 ? "" : "s");

        #if DEBUG_BLUETOOTH
        // // Serial.print("[BT] Device list complete - received ");
        // // Serial.print(deviceCount_);
        // // Serial.println(" devices");
        #endif

        // NOW fire event - we have the complete device list
        if (eventMgr_) {
            eventMgr_->fire(EventManager::EVENT_BT_SCAN_COMPLETE);
        }
    }
    else if (trimmed.startsWith("CONNECT:ATTEMPTING")) {
        connState_ = CONNECTING;
        currentOp_ = CONNECTING_TO_DEVICE;
        strcpy(statusMessage_, "Connecting...");
    }
    else if (trimmed.startsWith("STATE:")) {
        // STATUS command response - update connection state based on ESP32 state
        String state = trimmed.substring(6);
        ConnectionState oldState = connState_;

        if (state == "STREAMING") {
            connState_ = STREAMING;
            // // Serial.println("[BT] ESP32 is STREAMING (audio active)");
        } else if (state == "CONNECTED") {
            connState_ = CONNECTED;
            // // Serial.println("[BT] ESP32 is CONNECTED (audio may not be active)");
        } else if (state == "CONNECTING") {
            connState_ = CONNECTING;
        } else {
            // READY, SCANNING, UNINITIALIZED, etc. - not connected
            if (connState_ == CONNECTED || connState_ == STREAMING) {
                connState_ = NOT_CONNECTED;
            }
        }

        // Query device info if we just became connected
        if ((oldState == NOT_CONNECTED || oldState == CONNECTING) &&
            (connState_ == CONNECTED || connState_ == STREAMING)) {
            sendCommand("INFO");  // Get full connection info
        }
    }
    else if (trimmed.startsWith("CONNECTED:")) {
        handleConnected(trimmed.substring(10));
    }
    else if (trimmed.startsWith("DEVICE_NAME:")) {
        handleDeviceName(trimmed.substring(12));
    }
    else if (trimmed.startsWith("DISCONNECTED") || trimmed.startsWith("DISCONNECT:OK")) {
        handleDisconnected();
    }
    else if (trimmed.startsWith("AUTO_RECONNECT:")) {
        handleAutoReconnect(trimmed);
    }
    else if (trimmed.startsWith("AUTO_CONNECT_DEVICE:")) {
        handleAutoConnectDevice(trimmed.substring(20));
    }
    else if (trimmed.startsWith("VOLUME:SET:")) {
        int vol = trimmed.substring(11).toInt();
        handleVolumeSet(vol);
    }
    else if (trimmed.startsWith("VOLUME:")) {
        int vol = trimmed.substring(7).toInt();
        handleVolumeSet(vol);
    }
    else if (trimmed.startsWith("ERROR:")) {
        handleError(trimmed.substring(6));
    }
    else if (trimmed == "PONG" || trimmed == "TEST:OK") {
        // Connectivity test responses - ignore
    }
}

void BluetoothManager::handleSystemReady() {
    esp32Ready_ = true;
    strcpy(statusMessage_, "ESP32 ready");
}

void BluetoothManager::handleInitialized() {
    initialized_ = true;
    currentOp_ = IDLE;
    strcpy(statusMessage_, "Bluetooth ready");

    // Fire event
    if (eventMgr_) {
        eventMgr_->fire(EventManager::EVENT_BT_INITIALIZED);
    }

    // Query auto-connect device info
    queryAutoConnectDevice();
}

void BluetoothManager::handleScanStarted() {
    currentOp_ = SCANNING;
    strcpy(statusMessage_, "Scanning for devices...");

    // Fire event
    if (eventMgr_) {
        eventMgr_->fire(EventManager::EVENT_BT_SCAN_STARTED);
    }
}

void BluetoothManager::handleScanComplete() {
    strcpy(statusMessage_, "Scan complete - requesting device list...");
    // Request device list
    sendCommand("DEVICES");

    // Don't fire event yet - wait for DEVICES:END with complete list
}

void BluetoothManager::handleDevicesFound(int count) {
    sprintf(statusMessage_, "Found %d device%s", count, count == 1 ? "" : "s");
}

void BluetoothManager::handleDeviceList(const String& line) {
    receivingDeviceList_ = true;
    tempDeviceCount_ = 0;
    deviceCount_ = 0;
    memset(devices_, 0, sizeof(devices_));
}

void BluetoothManager::handleDevice(const String& deviceInfo) {
    if (tempDeviceCount_ >= MAX_DEVICES) return;

    // Parse: index,name,address,rssi
    int comma1 = deviceInfo.indexOf(',');
    if (comma1 < 0) return;

    int comma2 = deviceInfo.indexOf(',', comma1 + 1);
    if (comma2 < 0) return;

    int comma3 = deviceInfo.indexOf(',', comma2 + 1);
    if (comma3 < 0) return;

    // Extract fields
    String name = deviceInfo.substring(comma1 + 1, comma2);
    String address = deviceInfo.substring(comma2 + 1, comma3);
    int rssi = deviceInfo.substring(comma3 + 1).toInt();

    // Store device
    BTDevice& dev = devices_[tempDeviceCount_];
    name.toCharArray(dev.name, sizeof(dev.name));
    address.toCharArray(dev.address, sizeof(dev.address));
    dev.rssi = rssi;

    tempDeviceCount_++;
}

void BluetoothManager::handleConnected(const String& address) {
    connState_ = CONNECTED;
    currentOp_ = IDLE;
    address.toCharArray(connectedDeviceAddress_, sizeof(connectedDeviceAddress_));
    strcpy(statusMessage_, "Connected");

    // Fire event
    if (eventMgr_) {
        eventMgr_->fire(EventManager::EVENT_BT_CONNECTED);
    }
}

void BluetoothManager::handleDeviceName(const String& name) {
    name.toCharArray(connectedDeviceName_, sizeof(connectedDeviceName_));
    sprintf(statusMessage_, "Connected to %s", connectedDeviceName_);

    // Fire connected event again so UI can refresh with device name
    if (eventMgr_) {
        eventMgr_->fire(EventManager::EVENT_BT_CONNECTED);
    }
}

void BluetoothManager::handleDisconnected() {
    connState_ = NOT_CONNECTED;
    currentOp_ = IDLE;
    connectedDeviceName_[0] = '\0';
    connectedDeviceAddress_[0] = '\0';
    strcpy(statusMessage_, "Disconnected");

    // Fire event
    if (eventMgr_) {
        eventMgr_->fire(EventManager::EVENT_BT_DISCONNECTED);
    }
}

void BluetoothManager::handleAutoReconnect(const String& response) {
    if (response.endsWith("ON")) {
        autoReconnectEnabled_ = true;
    } else if (response.endsWith("OFF")) {
        autoReconnectEnabled_ = false;
    }
}

void BluetoothManager::handleAutoConnectDevice(const String& deviceInfo) {
    if (deviceInfo == "NONE") {
        autoConnectDeviceName_[0] = '\0';
        autoConnectDeviceAddress_[0] = '\0';
        return;
    }

    // Parse: name,address
    int comma = deviceInfo.indexOf(',');
    if (comma < 0) return;

    String name = deviceInfo.substring(0, comma);
    String address = deviceInfo.substring(comma + 1);

    name.toCharArray(autoConnectDeviceName_, sizeof(autoConnectDeviceName_));
    address.toCharArray(autoConnectDeviceAddress_, sizeof(autoConnectDeviceAddress_));
}

void BluetoothManager::handleError(const String& error) {
    hasError_ = true;
    error.toCharArray(errorMessage_, sizeof(errorMessage_));
    currentOp_ = IDLE;

    // Reset connection state on connection errors
    if (error.indexOf("CONNECT") >= 0 || error.indexOf("CONNECTION") >= 0) {
        connState_ = NOT_CONNECTED;
    }

    // Fire event with error message
    if (eventMgr_) {
        eventMgr_->fireStr(EventManager::EVENT_BT_ERROR, errorMessage_);
    }
}

void BluetoothManager::handleVolumeSet(int volume) {
    volume_ = constrain(volume, 0, 127);
}

// ============================================
// PUBLIC API
// ============================================

void BluetoothManager::sendInit() {
    currentOp_ = INITIALIZING;
    strcpy(statusMessage_, "Initializing...");
    sendCommand("INIT");
}

void BluetoothManager::sendStatus() {
    sendCommand("STATUS");
}

void BluetoothManager::startScan() {
    if (!initialized_) {
        strcpy(errorMessage_, "Not initialized");
        hasError_ = true;
        return;
    }

    // Clear any old data from Serial3 buffer before starting new scan
    while (Serial3.available()) {
        Serial3.read();
    }
    responseBuffer_ = "";  // Clear response buffer too

    currentOp_ = SCANNING;
    strcpy(statusMessage_, "Starting scan...");
    sendCommand("SCAN");
}

void BluetoothManager::stopScan() {
    sendCommand("SCAN_STOP");
}

const BluetoothManager::BTDevice* BluetoothManager::getDevice(int index) const {
    if (index < 0 || index >= deviceCount_) {
        return nullptr;
    }
    return &devices_[index];
}

void BluetoothManager::connectToDevice(const char* address) {
    if (!initialized_) {
        strcpy(errorMessage_, "Not initialized");
        hasError_ = true;
        return;
    }

    currentOp_ = CONNECTING_TO_DEVICE;
    connState_ = CONNECTING;
    sprintf(statusMessage_, "Connecting to %s...", address);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "CONNECT:%s", address);
    sendCommand(cmd);
}

void BluetoothManager::connectToDeviceIndex(int index) {
    if (index < 0 || index >= deviceCount_) {
        strcpy(errorMessage_, "Invalid device index");
        hasError_ = true;
        return;
    }

    const BTDevice* dev = getDevice(index);
    if (dev) {
        connectToDevice(dev->address);
    }
}

void BluetoothManager::disconnect() {
    currentOp_ = DISCONNECTING;
    strcpy(statusMessage_, "Disconnecting...");
    sendCommand("DISCONNECT");
}

void BluetoothManager::reconnect() {
    if (!initialized_) {
        strcpy(errorMessage_, "Not initialized");
        hasError_ = true;
        return;
    }

    currentOp_ = CONNECTING_TO_DEVICE;
    connState_ = CONNECTING;
    strcpy(statusMessage_, "Reconnecting...");
    sendCommand("RECONNECT");
}

void BluetoothManager::setAutoReconnect(bool enabled) {
    autoReconnectEnabled_ = enabled;
    sendCommand(enabled ? "AUTO_RECONNECT:ON" : "AUTO_RECONNECT:OFF");
}

void BluetoothManager::queryAutoConnectDevice() {
    sendCommand("AUTO_CONNECT_DEVICE?");
}

void BluetoothManager::setVolume(uint8_t volume) {
    volume_ = constrain(volume, 0, 127);

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "VOLUME:%d", volume_);
    sendCommand(cmd);
}

void BluetoothManager::queryStatus() {
    sendCommand("STATUS");
}

void BluetoothManager::queryAutoReconnectStatus() {
    sendCommand("AUTO_RECONNECT?");
}

void BluetoothManager::handleInfoLine(const String& line) {
    // Parse INFO response lines
    if (line.startsWith("DEVICE_NAME:")) {
        String name = line.substring(12);
        name.toCharArray(connectedDeviceName_, sizeof(connectedDeviceName_));
    }
    else if (line.startsWith("DEVICE_ADDR:")) {
        String addr = line.substring(12);
        addr.toCharArray(connectedDeviceAddress_, sizeof(connectedDeviceAddress_));
    }
    else if (line.startsWith("CONNECTED:YES")) {
        if (connState_ != CONNECTED && connState_ != STREAMING) {
            connState_ = CONNECTED;  // Will be updated to STREAMING when we see AUDIO_ACTIVE:YES
            if (eventMgr_) {
                eventMgr_->fire(EventManager::EVENT_BT_CONNECTED);
            }
        }
    }
    else if (line.startsWith("CONNECTED:NO")) {
        if (connState_ == CONNECTED || connState_ == STREAMING) {
            connState_ = NOT_CONNECTED;
            if (eventMgr_) {
                eventMgr_->fire(EventManager::EVENT_BT_DISCONNECTED);
            }
        }
    }
    else if (line.startsWith("AUDIO_ACTIVE:YES")) {
        if (connState_ == CONNECTED) {
            connState_ = STREAMING;
            // // Serial.println("[BT] Audio stream is active");
        }
    }
    else if (line.startsWith("AUDIO_ACTIVE:NO")) {
        if (connState_ == STREAMING) {
            connState_ = CONNECTED;
            // // Serial.println("[BT] WARNING: Audio stream NOT active!");
        }
    }
}
