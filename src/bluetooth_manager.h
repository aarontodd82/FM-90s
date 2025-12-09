#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <Arduino.h>

// Forward declare EventManager (avoid circular dependency)
class EventManager;

/**
 * BluetoothManager - Manages communication with ESP32 Bluetooth module via Serial3
 *
 * Protocol: See ESP32-For-Midi-Player/PROTOCOL.md for complete command reference
 *
 * Serial3 Configuration:
 * - Teensy Pin 14 (TX3) → ESP32 GPIO 16 (RX2)
 * - Teensy Pin 15 (RX3) → ESP32 GPIO 17 (TX2)
 * - Baud: 115200
 *
 * Event Integration:
 * - Fires events via EventManager when state changes occur
 * - See ui/framework/event_manager.h for event types
 */

class BluetoothManager {
public:
    // Bluetooth device info
    struct BTDevice {
        char name[64];
        char address[18];  // MAC address (XX:XX:XX:XX:XX:XX)
        int rssi;          // Signal strength
    };

    // Connection states
    enum ConnectionState {
        NOT_CONNECTED,
        CONNECTING,
        CONNECTED,
        STREAMING       // Connected and actively streaming audio
    };

    // Operation status
    enum OperationStatus {
        IDLE,
        INITIALIZING,
        SCANNING,
        CONNECTING_TO_DEVICE,
        DISCONNECTING
    };

    BluetoothManager();

    // Initialization
    void begin();
    bool initialize();  // Full initialization: RESET -> wait ESP32:READY -> INIT -> wait SYSTEM:READY
    void update();  // Call from main loop to process responses

    // Event integration (GUI Framework Phase 1)
    void setEventManager(EventManager* eventMgr) { eventMgr_ = eventMgr; }

    // System commands
    void sendInit();                  // Initialize ESP32 BT subsystems
    void sendStatus();                // Query current state
    bool isInitialized() const { return initialized_; }
    bool isESP32Ready() const { return esp32Ready_; }  // Has ESP32 finished booting?

    // Scanning
    void startScan();                 // Start device scan
    void stopScan();                  // Stop scanning
    bool isScanning() const { return currentOp_ == SCANNING; }
    int getDeviceCount() const { return deviceCount_; }
    const BTDevice* getDevice(int index) const;

    // Connection management
    void connectToDevice(const char* address);  // Connect by MAC address
    void connectToDeviceIndex(int index);       // Connect by device list index
    void disconnect();                          // Disconnect current device
    void reconnect();                           // Reconnect to last device

    ConnectionState getConnectionState() const { return connState_; }
    bool isConnected() const { return connState_ == CONNECTED || connState_ == STREAMING; }
    bool isStreaming() const { return connState_ == STREAMING; }
    const char* getConnectedDeviceName() const { return connectedDeviceName_; }
    const char* getConnectedDeviceAddress() const { return connectedDeviceAddress_; }

    // Auto-reconnect settings
    void setAutoReconnect(bool enabled);
    bool getAutoReconnect() const { return autoReconnectEnabled_; }

    // Query saved device for auto-reconnect
    void queryAutoConnectDevice();
    bool hasAutoConnectDevice() const { return strlen(autoConnectDeviceName_) > 0; }
    const char* getAutoConnectDeviceName() const { return autoConnectDeviceName_; }
    const char* getAutoConnectDeviceAddress() const { return autoConnectDeviceAddress_; }

    // Volume control
    void setVolume(uint8_t volume);   // 0-127
    uint8_t getVolume() const { return volume_; }

    // Status information
    OperationStatus getCurrentOperation() const { return currentOp_; }
    const char* getStatusMessage() const { return statusMessage_; }
    bool hasError() const { return hasError_; }
    const char* getErrorMessage() const { return errorMessage_; }

    // Query commands for current state
    void queryStatus();              // Send STATUS command
    void queryAutoReconnectStatus(); // Send AUTO_RECONNECT? command

private:
    // Serial communication
    void sendCommand(const char* cmd);
    void processResponse();
    void parseLine(const String& line);

    // Response handlers
    void handleSystemReady();
    void handleInitialized();
    void handleScanStarted();
    void handleScanComplete();
    void handleDevicesFound(int count);
    void handleDevice(const String& deviceInfo);
    void handleDeviceList(const String& line);
    void handleConnected(const String& address);
    void handleDeviceName(const String& name);
    void handleDisconnected();
    void handleAutoReconnect(const String& response);
    void handleAutoConnectDevice(const String& deviceInfo);
    void handleError(const String& error);
    void handleVolumeSet(int volume);

    // State
    bool initialized_;
    bool esp32Ready_;  // Has ESP32 sent ESP32:READY?
    ConnectionState connState_;
    OperationStatus currentOp_;
    EventManager* eventMgr_;  // Event manager for GUI notifications (Phase 1)

    // Device list (from last scan)
    static const int MAX_DEVICES = 20;
    BTDevice devices_[MAX_DEVICES];
    int deviceCount_;
    bool receivingDeviceList_;
    int tempDeviceCount_;  // Count while receiving list

    // Info parsing
    bool receivingInfo_;
    void handleInfoLine(const String& line);

    // Connected device info
    char connectedDeviceName_[64];
    char connectedDeviceAddress_[18];

    // Auto-connect device info (saved on ESP32)
    char autoConnectDeviceName_[64];
    char autoConnectDeviceAddress_[18];
    bool autoReconnectEnabled_;

    // Volume
    uint8_t volume_;

    // Status/Error tracking
    char statusMessage_[128];
    char errorMessage_[128];
    bool hasError_;

    // Response buffer
    String responseBuffer_;
    unsigned long lastCommandTime_;
    static const unsigned long COMMAND_TIMEOUT = 20000;  // 20 seconds (scan takes 15s + response time)
};

#endif // BLUETOOTH_MANAGER_H
