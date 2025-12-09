#include "usb_drive_manager.h"
#include "file_browser.h"

USBDriveManager::USBDriveManager(FileBrowser* browser, USBHost& usbHost, USBHub& hub, USBDrive& drive, USBFilesystem& fs)
  : browser_(browser)
  , myusb_(usbHost)
  , hub1_(hub)
  , msDrive1_(drive)
  , myFS_(fs)
  , driveActive_(false)
  , onConnected_(nullptr)
  , onDisconnected_(nullptr) {
}

void USBDriveManager::begin() {
  Serial.println("[USB] Initializing USB Host...");

  // Enable USB Host hardware (recommended by Teensy best practices)
  myusb_.begin();

  Serial.println("[USB] USB Host initialized - hot-plug detection enabled");
  Serial.println("[USB] Call update() in main loop to detect drive changes");
}

void USBDriveManager::update() {
  // Call Task() to update driver status (required for hot-plug detection)
  myusb_.Task();

  // Check filesystem status (this is what the official examples check)
  // The myFS_ (USBFilesystem) object evaluates to true when a drive is ready
  bool currentlyActive = (bool)myFS_;

  // Debug: Print status every 5 seconds
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 5000) {
    Serial.printf("[USB Debug] myFS_=%d, msDrive1_=%d, driveActive_=%d\n",
                  (bool)myFS_, (bool)msDrive1_, driveActive_);
    lastDebug = millis();
  }

  // Detect state change
  if (currentlyActive != driveActive_) {
    if (currentlyActive) {
      // Drive just connected
      Serial.println("[USB] *** USB Drive CONNECTED ***");
      Serial.printf("[USB] myFS_ is now TRUE (drive ready for files)\n");
      driveActive_ = true;

      // Fire callback if registered
      if (onConnected_) {
        Serial.println("[USB] Firing onConnected callback...");
        onConnected_();
      }
    } else {
      // Drive just disconnected
      Serial.println("[USB] *** USB Drive DISCONNECTED ***");
      Serial.printf("[USB] myFS_ is now FALSE (no drive)\n");
      driveActive_ = false;

      // Fire callback if registered
      if (onDisconnected_) {
        Serial.println("[USB] Firing onDisconnected callback...");
        onDisconnected_();
      }
    }
  }
}

bool USBDriveManager::checkIfReady() {
  Serial.println("[USB] Manual check for USB drive...");

  // Call Task() a few times to allow enumeration to complete
  // This provides a fallback if update() isn't being called regularly
  for (int i = 0; i < 10; i++) {
    myusb_.Task();
    delay(10);
  }

  // Check if filesystem is available
  bool ready = myFS_;

  // Update cached state
  driveActive_ = ready;

  if (ready) {
    Serial.println("[USB] USB drive detected and ready!");
  } else {
    Serial.println("[USB] No USB drive detected");
  }

  return ready;
}

FS* USBDriveManager::getFilesystem() {
  // Check if filesystem is available
  if (!myFS_) {
    return nullptr;
  }

  // Return pointer to USB filesystem
  return &myFS_;
}
