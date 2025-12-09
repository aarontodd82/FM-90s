#ifndef BLUETOOTH_ASYNC_OPERATIONS_H
#define BLUETOOTH_ASYNC_OPERATIONS_H

#include "ui/framework/async_operation.h"
#include "bluetooth_manager.h"

/**
 * BluetoothScanOperation - Async wrapper for Bluetooth device scanning
 *
 * Usage:
 *   BluetoothScanOperation* scan = new BluetoothScanOperation(btManager);
 *   scan->start();
 *
 *   void loop() {
 *       scan->update();
 *       if (scan->isDone()) {
 *           if (scan->isSuccess()) {
 *               int count = btManager->getDeviceCount();
 *               // ... show results ...
 *           }
 *           delete scan;
 *       }
 *   }
 */
class BluetoothScanOperation : public AsyncOperation {
public:
    /**
     * Create a Bluetooth scan operation
     * @param btMgr - BluetoothManager instance
     * @param timeoutMs - Scan timeout (default: 25 seconds, ESP32 scan takes ~20s + response time)
     */
    BluetoothScanOperation(BluetoothManager* btMgr, unsigned long timeoutMs = 25000)
        : AsyncOperation("Scanning for Bluetooth devices", timeoutMs),
          btManager_(btMgr),
          scanComplete_(false) {}

    void start() override {
        scanComplete_ = false;
        btManager_->startScan();
        AsyncOperation::start();
    }

    // Called by screen when EVENT_BT_SCAN_COMPLETE is received
    void markComplete() {
        scanComplete_ = true;
    }

protected:
    bool poll() override {
        // Scan is complete when we receive the scan complete event
        // Don't rely on isScanning() as it may change state too quickly
        return scanComplete_;
    }

    void onComplete() override {
        // // Serial.print("[BluetoothScan] Found ");
        // // Serial.print(btManager_->getDeviceCount());
        // // Serial.println(" devices");
    }

    void onCancel() override {
        btManager_->stopScan();
    }

    void onFailed() override {
        // Stop scan on timeout/failure
        if (btManager_->isScanning()) {
            btManager_->stopScan();
        }
    }

private:
    BluetoothManager* btManager_;
    bool scanComplete_;
};

/**
 * BluetoothConnectOperation - Async wrapper for Bluetooth device connection
 *
 * Usage:
 *   BluetoothConnectOperation* connect =
 *       new BluetoothConnectOperation(btManager, deviceAddress);
 *   connect->start();
 *
 *   void loop() {
 *       connect->update();
 *       if (connect->isDone()) {
 *           if (connect->isSuccess()) {
 *               // Connected!
 *           } else {
 // *               // Serial.println(connect->getErrorMessage());
 *           }
 *           delete connect;
 *       }
 *   }
 */
class BluetoothConnectOperation : public AsyncOperation {
public:
    /**
     * Create a Bluetooth connect operation
     * @param btMgr - BluetoothManager instance
     * @param address - Device MAC address (XX:XX:XX:XX:XX:XX)
     * @param timeoutMs - Connection timeout (default: 15 seconds)
     */
    BluetoothConnectOperation(BluetoothManager* btMgr, const char* address,
                             unsigned long timeoutMs = 15000)
        : AsyncOperation("Connecting to Bluetooth device", timeoutMs),
          btManager_(btMgr) {
        strncpy(deviceAddress_, address, sizeof(deviceAddress_) - 1);
        deviceAddress_[sizeof(deviceAddress_) - 1] = '\0';
    }

    void start() override {
        btManager_->connectToDevice(deviceAddress_);
        AsyncOperation::start();
    }

protected:
    bool poll() override {
        // Check if connected
        if (btManager_->isConnected()) {
            return true;
        }

        // Check if BluetoothManager has an error
        if (btManager_->hasError()) {
            setError(btManager_->getErrorMessage());
            return true;  // Operation is "done" (with failure)
        }

        return false;  // Still connecting
    }

    void onComplete() override {
        // // Serial.print("[BluetoothConnect] Connected to: ");
        // // Serial.println(btManager_->getConnectedDeviceName());
    }

    void onFailed() override {
        // // Serial.print("[BluetoothConnect] Failed: ");
        // // Serial.println(getErrorMessage());
    }

private:
    BluetoothManager* btManager_;
    char deviceAddress_[18];
};

/**
 * BluetoothDisconnectOperation - Async wrapper for Bluetooth disconnection
 *
 * Usage:
 *   BluetoothDisconnectOperation* disconnect =
 *       new BluetoothDisconnectOperation(btManager);
 *   disconnect->start();
 */
class BluetoothDisconnectOperation : public AsyncOperation {
public:
    /**
     * Create a Bluetooth disconnect operation
     * @param btMgr - BluetoothManager instance
     * @param timeoutMs - Disconnection timeout (default: 5 seconds)
     */
    BluetoothDisconnectOperation(BluetoothManager* btMgr,
                                unsigned long timeoutMs = 5000)
        : AsyncOperation("Disconnecting Bluetooth device", timeoutMs),
          btManager_(btMgr) {}

    void start() override {
        btManager_->disconnect();
        AsyncOperation::start();
    }

protected:
    bool poll() override {
        // Disconnection is complete when no longer connected
        return !btManager_->isConnected();
    }

    void onComplete() override {
        // // Serial.println("[BluetoothDisconnect] Disconnected successfully");
    }

private:
    BluetoothManager* btManager_;
};

#endif // BLUETOOTH_ASYNC_OPERATIONS_H
