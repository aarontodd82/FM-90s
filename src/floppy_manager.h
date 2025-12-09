#pragma once
#include <Arduino.h>
#include <SD.h>
#include <vector>

// Forward declare to avoid circular dependency
class FileBrowser;

// File entry from floppy disk
struct FloppyFileEntry {
  String name;      // 8.3 filename
  uint32_t size;    // File size in bytes
  bool isDir;       // true if directory, false if file
};

class FloppyManager {
public:
  FloppyManager(FileBrowser* browser);

  // Initialize serial communication with Arduino floppy controller
  void begin();

  // Update - handles serial communication and disk change detection (call from loop)
  void update();

  // Check if Arduino floppy controller is ready
  bool isFloppyConnected() const { return controllerReady_; }

  // Check if a disk is currently inserted and ready
  bool isDiskReady() const { return diskReady_; }

  // Check if currently requesting file list (async operation in progress)
  bool isRequestingFileList() const { return state_ == STATE_LIST_WAIT; }

  // Check disk status (sends DISKCHG? command and waits for response)
  bool checkDiskStatus();

  // Request file list from floppy (sends LIST command)
  bool requestFileList();

  // Get list of files from last LIST command
  const std::vector<FloppyFileEntry>& getFileList() const { return fileList_; }

  // Clear cached file list (forces refresh on next request)
  void clearFileListCache() { fileList_.clear(); }

  // Get current state as string (for debugging)
  const char* getStateString() const;

  // Get list of music files only
  std::vector<FloppyFileEntry> getMusicFiles() const;

  // Transfer a file from floppy to SD card via XMODEM (BLOCKING)
  // Returns path to transferred file on success, empty string on failure
  String transferFile(const char* floppyFilename);

  // === ASYNC TRANSFER API (for use with FloppyTransferOperation) ===

  // Start async file transfer from floppy to SD card
  // Returns true if transfer initiated successfully, false if error
  bool getFile(const char* floppyFilename);

  // Check if async transfer is complete (success or failure)
  bool isTransferComplete() const { return asyncTransferState_ == TRANSFER_COMPLETE || asyncTransferState_ == TRANSFER_FAILED; }

  // Check if async transfer failed
  bool hasTransferError() const { return asyncTransferState_ == TRANSFER_FAILED; }

  // Get error message from failed transfer
  const char* getTransferError() const { return asyncTransferError_.c_str(); }

  // Get transfer progress (0-100 percent)
  int getTransferProgress() const { return asyncTransferProgress_; }

  // Cancel ongoing async transfer
  void cancelTransfer();

  // Get destination path of async transfer (valid after completion)
  const char* getAsyncTransferPath() const { return asyncTransferDestPath_.c_str(); }

  // Clean up temporary files (call on startup and after playback)
  void cleanupTempFiles();

  // Get the path to the last transferred file
  const String& getLastTransferredFile() const { return lastTransferredFile_; }

private:
  FileBrowser* browser_;

  // Hardware Serial for Arduino communication (Serial4 = pins 16 RX, 17 TX)
  HardwareSerial* serial_;

  // Protocol state
  enum State {
    STATE_INIT,           // Waiting for initial READY
    STATE_READY,          // Controller ready, waiting for commands
    STATE_LIST_WAIT,      // Sent LIST, waiting for file entries
    STATE_GET_WAIT,       // Sent GET, waiting for file transfer
    STATE_ERROR           // Communication error
  };

  State state_;
  bool controllerReady_;
  bool diskReady_;

  // File management
  std::vector<FloppyFileEntry> fileList_;
  String lastTransferredFile_;  // Path to last transferred file on SD card

  // Serial communication
  String lineBuffer_;
  uint32_t lastActivityTime_;

  // Transfer state (for blocking transferFile)
  String transferFilename_;     // Filename being transferred
  String transferDestPath_;     // Destination path for current transfer

  // Async transfer state (for non-blocking getFile)
  enum AsyncTransferState {
    TRANSFER_IDLE,      // No transfer in progress
    TRANSFER_STARTING,  // Sent GET command, waiting for OK
    TRANSFER_RUNNING,   // Receiving file via XMODEM
    TRANSFER_COMPLETE,  // Transfer completed successfully
    TRANSFER_FAILED     // Transfer failed with error
  };

  AsyncTransferState asyncTransferState_;
  String asyncTransferFilename_;   // Filename being transferred (async)
  String asyncTransferDestPath_;   // Destination path (async)
  String asyncTransferError_;      // Error message if transfer failed
  int asyncTransferProgress_;      // Progress percentage (0-100)
  unsigned long asyncTransferStartTime_;  // When transfer started

  // XMODEM state (for async transfers)
  class XModemFileReceiver* xmodemReceiver_;  // Forward declare to avoid header dependency
  bool xmodemActive_;

  // Helper functions
  void processIncomingData();
  void updateAsyncTransfer();  // Update async transfer state machine
  bool waitForLine(String& line, uint32_t timeoutMs);
  void handleLine(const String& line);
  void sendCommand(const char* cmd);
  bool isMusicFile(const char* filename) const;

  // Temporary file management
  bool ensureTempDirectory();
  void deleteTempDirectory();

  // Constants
  static constexpr uint32_t SERIAL_BAUD = 115200;
  static constexpr uint32_t COMMAND_TIMEOUT_MS = 15000;  // 15 seconds for initial handshake
  static constexpr size_t MAX_LINE_LENGTH = 256;
  static constexpr const char* TEMP_DIR = "/TEMP";
};