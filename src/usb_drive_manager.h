#pragma once
#include <Arduino.h>
#include <USBHost_t36.h>
#include <SD.h>
#include <vector>

// Forward declare to avoid circular dependency
class FileBrowser;

// File entry from USB drive (kept for legacy compatibility)
struct USBFileEntry {
  String name;      // Filename
  uint32_t size;    // File size in bytes
  bool isDir;       // true if directory, false if file
};

/**
 * USBDriveManager - Automatic USB hot-plug detection
 *
 * Design: Uses proper Teensy USBHost_t36 pattern with automatic connection detection.
 * Detects plug/unplug events and fires callbacks for event-driven UI updates.
 *
 * Usage:
 *   - Call begin() once during initialization (enables USB Host)
 *   - Call update() in main loop (polls driver status)
 *   - Register callbacks for connection/disconnection events
 *   - Use getFilesystem() to access files when connected
 *   - Manual checkIfReady() still available as fallback
 */
class USBDriveManager {
public:
  // Connection status change callback
  typedef void (*ConnectionCallback)();

  USBDriveManager(FileBrowser* browser, USBHost& usbHost, USBHub& hub, USBDrive& drive, USBFilesystem& fs);

  // Initialize USB Host hardware (call once during setup)
  // Enables USB Host and powers on USB port
  void begin();

  // Update USB driver status and detect plug/unplug events (call in main loop)
  // Fires callbacks when connection status changes
  void update();

  // Check if USB drive is currently available
  // Returns true if drive is ready, false otherwise
  bool checkIfReady();

  // Check if USB drive is connected (cached status, updated by update())
  bool isDriveReady() const { return driveActive_; }

  // Get the active USB filesystem (returns nullptr if not ready)
  FS* getFilesystem();

  // Register callbacks for connection status changes
  void setOnConnected(ConnectionCallback callback) { onConnected_ = callback; }
  void setOnDisconnected(ConnectionCallback callback) { onDisconnected_ = callback; }

  // Legacy MenuSystem compatibility (not used, kept for compilation)
  bool requestFileList() { return false; }
  std::vector<USBFileEntry> getMusicFiles() const { return std::vector<USBFileEntry>(); }

private:
  FileBrowser* browser_;

  // USB Host (references to external global objects)
  USBHost& myusb_;
  USBHub& hub1_;
  USBDrive& msDrive1_;
  USBFilesystem& myFS_;

  // Connection tracking (for hot-plug detection)
  bool driveActive_;

  // Callbacks for connection status changes
  ConnectionCallback onConnected_;
  ConnectionCallback onDisconnected_;

  // File management (legacy)
  std::vector<USBFileEntry> fileList_;
};
