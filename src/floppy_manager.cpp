#include "floppy_manager.h"
#include "file_browser.h"
#include "xmodem_wrapper.h"

FloppyManager::FloppyManager(FileBrowser* browser)
  : browser_(browser)
  , serial_(&Serial4)
  , state_(STATE_INIT)
  , controllerReady_(false)
  , diskReady_(false)
  , lastActivityTime_(0)
  , asyncTransferState_(TRANSFER_IDLE)
  , asyncTransferProgress_(0)
  , asyncTransferStartTime_(0)
  , xmodemReceiver_(nullptr)
  , xmodemActive_(false) {
}

void FloppyManager::begin() {
  // // Serial.println("Initializing Arduino floppy controller interface...");
  // // Serial.println("Communication via Hardware Serial on pins 16 (RX), 17 (TX)");

  // Initialize Serial4 (pins 16 RX, 17 TX)
  serial_->begin(SERIAL_BAUD);

  // Small delay for serial to stabilize
  delay(100);

  // Clear any pending data from previous sessions
  while (serial_->available()) {
    serial_->read();
  }

  state_ = STATE_INIT;
  lineBuffer_ = "";
  lastActivityTime_ = millis();

  // // Serial.println("Hardware serial initialized. Probing Arduino controller...");
}

void FloppyManager::update() {
  // Process any incoming serial data
  processIncomingData();

  // Active probing during initialization - send STATUS every 2 seconds
  static uint32_t lastProbe = 0;
  if (state_ == STATE_INIT && millis() - lastProbe > 2000) {
    lastProbe = millis();
    sendCommand("STATUS");
  }

  // Handle state machine timeouts
  if (state_ != STATE_READY && state_ != STATE_INIT) {
    if (millis() - lastActivityTime_ > COMMAND_TIMEOUT_MS) {
      // // Serial.println("ERROR: Floppy controller timeout!");
      state_ = STATE_ERROR;
      controllerReady_ = false;
    }
  }

  // Handle async transfer state machine
  updateAsyncTransfer();
}

void FloppyManager::processIncomingData() {
  // Process incoming data
  while (serial_->available()) {
    char c = serial_->read();
    lastActivityTime_ = millis();

    // Handle line endings (looking for \n, ignoring \r)
    if (c == '\n') {
      // Remove any trailing \r
      lineBuffer_.trim();

      if (lineBuffer_.length() > 0) {
        handleLine(lineBuffer_);
        lineBuffer_ = "";
      }
    } else if (c != '\r' && lineBuffer_.length() < MAX_LINE_LENGTH) {
      lineBuffer_ += c;
    }
  }
}

void FloppyManager::handleLine(const String& line) {
  switch (state_) {
    case STATE_INIT:
      // Accept OK (from STATUS) or FDC-USB Ready (from boot)
      if (line == "OK" || line == "FDC-USB Ready") {
        // // Serial.println("Floppy controller connected and ready.");
        state_ = STATE_READY;
        controllerReady_ = true;
        // Note: Disk state is checked on-demand when entering floppy browser
      }
      // Ignore other messages during init
      break;

    case STATE_READY:
      // No asynchronous disk messages anymore - must explicitly check
      // Just ignore any unexpected lines in ready state
      // // Serial.print("[READY] Unexpected line: ");
      // // Serial.println(line);
      break;

    case STATE_GET_WAIT:
      // File transfer in progress - handled by transferFile() method
      // This state should not receive lines during XMODEM transfer
      // // Serial.print("[GET_WAIT] Unexpected line: ");
      // // Serial.println(line);
      break;

    case STATE_LIST_WAIT:
      // Parsing LIST response
      if (line == "END") {
        // // Serial.println("\n=== File List Complete ===");
        // // Serial.print("Total files/directories: ");
        // // Serial.println(fileList_.size());

        // Filter and show music files
        auto musicFiles = getMusicFiles();
        // // Serial.print("Music files: ");
        // // Serial.println(musicFiles.size());

        if (musicFiles.size() > 0) {
          // // Serial.println("\nMusic files found:");
          for (size_t i = 0; i < musicFiles.size(); i++) {
            // // Serial.print("  [");
            // // Serial.print(i + 1);
            // // Serial.print("] ");
            // // Serial.print(musicFiles[i].name);
            // // Serial.print(" (");
            // // Serial.print(musicFiles[i].size);
            // // Serial.println(" bytes)");
          }
          diskReady_ = true;
        } else {
          // // Serial.println("\nNo music files found on disk.");
          diskReady_ = false;
        }

        state_ = STATE_READY;
      } else if (line.startsWith("ERR")) {
        // // Serial.print("Error reading disk: ");
        // // Serial.println(line);
        state_ = STATE_READY;
        diskReady_ = false;
      } else {
        // Parse file entry: filename|size|attrs
        int pipe1 = line.indexOf('|');
        int pipe2 = line.indexOf('|', pipe1 + 1);

        if (pipe1 > 0 && pipe2 > pipe1) {
          FloppyFileEntry entry;
          entry.name = line.substring(0, pipe1);
          entry.size = line.substring(pipe1 + 1, pipe2).toInt();
          String attrs = line.substring(pipe2 + 1);
          entry.isDir = (attrs == "D");

          fileList_.push_back(entry);
        }
      }
      break;

    case STATE_ERROR:
      // In error state, log any messages but don't process
      // // Serial.print("[ERROR STATE] ");
      // // Serial.println(line);
      break;
  }
}

bool FloppyManager::checkDiskStatus() {
  if (!controllerReady_) {
    // // Serial.println("Controller not ready!");
    return false;
  }

  // Send DISKCHG? command
  sendCommand("DISKCHG?");

  // Wait for response (DISK_IN or DISK_OUT)
  String response;
  if (!waitForLine(response, 2000)) {
    // // Serial.println("ERROR: Timeout waiting for disk status");
    diskReady_ = false;
    return false;
  }

  if (response == "DISK_IN") {
    // // Serial.println("Disk present");
    diskReady_ = true;
    return true;
  } else if (response == "DISK_OUT") {
    // // Serial.println("No disk");
    diskReady_ = false;
    fileList_.clear();
    return true;
  } else {
    // // Serial.print("ERROR: Unexpected response to DISKCHG?: ");
    // // Serial.println(response);
    diskReady_ = false;
    return false;
  }
}

bool FloppyManager::requestFileList() {
  if (state_ != STATE_READY) {
    // // Serial.println("Controller not ready for commands!");
    return false;
  }

  // Check disk status first
  if (!checkDiskStatus() || !diskReady_) {
    // // Serial.println("No disk inserted!");
    return false;
  }

  // // Serial.println("Sending LIST command...");
  fileList_.clear();
  sendCommand("LIST");
  state_ = STATE_LIST_WAIT;
  lastActivityTime_ = millis();

  return true;
}

std::vector<FloppyFileEntry> FloppyManager::getMusicFiles() const {
  std::vector<FloppyFileEntry> musicFiles;

  for (const auto& entry : fileList_) {
    if (!entry.isDir && isMusicFile(entry.name.c_str())) {
      musicFiles.push_back(entry);
    }
  }

  return musicFiles;
}

void FloppyManager::sendCommand(const char* cmd) {
  serial_->print(cmd);
  serial_->print("\r\n");
  serial_->flush();
}

bool FloppyManager::isMusicFile(const char* filename) const {
  if (!filename) return false;

  String name = String(filename);
  name.toLowerCase();

  // Check for supported music file extensions
  return name.endsWith(".mid") ||
         name.endsWith(".midi") ||
         name.endsWith(".smf") ||
         name.endsWith(".kar") ||
         name.endsWith(".vgm") ||
         name.endsWith(".vgz") ||
         name.endsWith(".dro") ||
         name.endsWith(".imf") ||
         name.endsWith(".wlf") ||
         name.endsWith(".rad");
}

bool FloppyManager::waitForLine(String& line, uint32_t timeoutMs) {
  uint32_t startTime = millis();
  lineBuffer_ = "";

  while (millis() - startTime < timeoutMs) {
    if (serial_->available()) {
      char c = serial_->read();

      if (c == '\n') {
        lineBuffer_.trim();
        if (lineBuffer_.length() > 0) {
          line = lineBuffer_;
          lineBuffer_ = "";
          return true;
        }
      } else if (c != '\r' && lineBuffer_.length() < MAX_LINE_LENGTH) {
        lineBuffer_ += c;
      }
    }
    delay(1);
  }

  return false;
}

const char* FloppyManager::getStateString() const {
  switch(state_) {
    case STATE_INIT: return "Waiting for controller";
    case STATE_READY: return "Ready";
    case STATE_LIST_WAIT: return "Reading disk";
    case STATE_GET_WAIT: return "Transferring file";
    case STATE_ERROR: return "Error";
    default: return "Unknown";
  }
}

String FloppyManager::transferFile(const char* floppyFilename) {
  if (state_ != STATE_READY) {
    // // Serial.println("ERROR: Controller not ready for file transfer!");
    return "";
  }

  // Check disk status before attempting transfer
  if (!checkDiskStatus() || !diskReady_) {
    // // Serial.println("ERROR: No disk inserted!");
    return "";
  }

  // Ensure temp directory exists
  if (!ensureTempDirectory()) {
    // // Serial.println("ERROR: Failed to create temp directory!");
    return "";
  }

  // Build destination path
  transferFilename_ = String(floppyFilename);
  transferDestPath_ = String(TEMP_DIR) + "/" + floppyFilename;

  // // Serial.println("\n=== Starting File Transfer ===");
  // // Serial.print("Source (floppy): ");
  // // Serial.println(floppyFilename);
  // // Serial.print("Destination (SD): ");
  // // Serial.println(transferDestPath_);

  // Clear any leftover data from previous operations BEFORE sending command
  // CRITICAL: Must clear BOTH serial buffer AND lineBuffer_!
  while (serial_->available()) {
    serial_->read();
  }
  lineBuffer_ = "";  // Clear the line buffer to prevent interference from previous operations

  // Send GET command
  String getCommand = "GET " + String(floppyFilename);
  sendCommand(getCommand.c_str());
  state_ = STATE_GET_WAIT;
  lastActivityTime_ = millis();

  // Wait for OK or ERR response
  String response;
  if (!waitForLine(response, 5000)) {
    // // Serial.println("ERROR: Timeout waiting for GET response");
    lineBuffer_ = "";  // Clear buffer on error
    state_ = STATE_READY;
    return "";
  }

  if (response.startsWith("ERR")) {
    // // Serial.print("ERROR from controller: ");
    // // Serial.println(response);
    lineBuffer_ = "";  // Clear buffer on error
    state_ = STATE_READY;
    return "";
  }

  if (response != "OK") {
    // // Serial.print("ERROR: Unexpected response: ");
    // // Serial.println(response);
    lineBuffer_ = "";  // Clear buffer on error
    state_ = STATE_READY;
    return "";
  }

  // // Serial.println("Controller ready to send file via XMODEM...");

  // Brief pause to let Arduino set up XMODEM
  delay(50);

  // Receive file via XMODEM using proven library
  XModemFileReceiver xmodem(serial_);
  bool success = xmodem.receiveFile(transferDestPath_.c_str());

  if (success) {
    // // Serial.println("Transfer completed successfully!");

    // CRITICAL: Small delay to ensure SD card has fully synced the file
    // before we try to read it back. SD card write caching can cause issues.
    delay(100);

    // Verify file was written correctly
    if (SD.exists(transferDestPath_.c_str())) {
      File verifyFile = SD.open(transferDestPath_.c_str(), FILE_READ);
      if (verifyFile) {
        uint32_t fileSize = verifyFile.size();
        verifyFile.close();
        // // Serial.print("File verification: size = ");
        // // Serial.print(fileSize);
        // // Serial.print(" bytes (expected: ");
        // // Serial.print(xmodem.getBytesReceived());
        // // Serial.println(")");

        if (fileSize != xmodem.getBytesReceived()) {
          // // Serial.println("ERROR: File size mismatch! File may be corrupted.");
          // Continue anyway to see what happens
        }
      } else {
        // // Serial.println("WARNING: Could not open file for verification");
      }
    } else {
      // // Serial.println("ERROR: File was not created on SD card!");
    }

    // Wait for Arduino to send "DONE\r\n" message
    String doneLine;
    if (waitForLine(doneLine, 2000)) {
      if (doneLine == "DONE") {
        // // Serial.println("Arduino confirmed transfer complete");
      } else {
        // // Serial.print("Unexpected response after transfer: ");
        // // Serial.println(doneLine);
      }
    } else {
      // // Serial.println("Warning: No DONE message from Arduino (continuing anyway)");
    }

    // Clear any remaining bytes
    delay(50);
    while (serial_->available()) {
      serial_->read();
    }
    lineBuffer_ = "";  // Clear line buffer for next operation

    lastTransferredFile_ = transferDestPath_;
    state_ = STATE_READY;
    return transferDestPath_;

  } else {
    // // Serial.println("ERROR: XMODEM transfer failed!");
    // // Serial.print("Reason: ");
    // // Serial.println(xmodem.getErrorMessage());

    // Clear any leftover bytes from failed transfer
    delay(100);
    while (serial_->available()) {
      serial_->read();
    }
    lineBuffer_ = "";  // Clear line buffer after failed transfer

    state_ = STATE_READY;
    return "";
  }
}

void FloppyManager::cleanupTempFiles() {
  // // Serial.println("Cleaning up temporary floppy files...");

  File tempDir = SD.open(TEMP_DIR);
  if (!tempDir) {
    // // Serial.println("No temp directory found (nothing to clean up)");
    return;
  }

  if (!tempDir.isDirectory()) {
    // // Serial.println("TEMP is not a directory!");
    tempDir.close();
    return;
  }

  int deletedCount = 0;

  // Delete all files in temp directory
  while (true) {
    File entry = tempDir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      String path = String(TEMP_DIR) + "/" + entry.name();
      entry.close();

      if (SD.remove(path.c_str())) {
        // // Serial.print("Deleted: ");
        // // Serial.println(path);
        deletedCount++;
      } else {
        // // Serial.print("Failed to delete: ");
        // // Serial.println(path);
      }
    } else {
      entry.close();
    }
  }

  tempDir.close();

  // // Serial.print("Cleanup complete. Deleted ");
  // // Serial.print(deletedCount);
  // // Serial.println(" file(s)");

  lastTransferredFile_ = "";
}

bool FloppyManager::ensureTempDirectory() {
  // Check if directory exists
  if (SD.exists(TEMP_DIR)) {
    File dir = SD.open(TEMP_DIR);
    if (dir && dir.isDirectory()) {
      dir.close();
      return true;
    }
    dir.close();

    // Exists but not a directory - delete it
    SD.remove(TEMP_DIR);
  }

  // Create directory
  if (SD.mkdir(TEMP_DIR)) {
    // // Serial.println("Created temp directory: /TEMP");
    return true;
  } else {
    // // Serial.println("Failed to create temp directory!");
    return false;
  }
}

void FloppyManager::deleteTempDirectory() {
  cleanupTempFiles();

  // Try to remove the directory itself
  if (SD.rmdir(TEMP_DIR)) {
    // // Serial.println("Removed temp directory");
  }
}

// ============================================================================
// ASYNC TRANSFER IMPLEMENTATION (for FloppyTransferOperation)
// ============================================================================

bool FloppyManager::getFile(const char* floppyFilename) {
  // TODO: Async transfer not yet implemented
  // XModemFileReceiver is currently blocking-only
  // Use transferFile() instead for now

  (void)floppyFilename;  // Suppress unused parameter warning

  asyncTransferError_ = "Async transfer not implemented - use transferFile() instead";
  asyncTransferState_ = TRANSFER_FAILED;
  // // Serial.println("[FloppyAsync] ERROR: Async transfer not yet implemented");
  // // Serial.println("[FloppyAsync] Use blocking transferFile() method instead");

  return false;
}

void FloppyManager::cancelTransfer() {
  // TODO: Async transfer not yet implemented
  // For now, just mark as failed if somehow an async transfer was started

  if (asyncTransferState_ != TRANSFER_IDLE) {
    // // Serial.println("[FloppyAsync] Canceling stub transfer");
    asyncTransferError_ = "Transfer canceled";
    asyncTransferState_ = TRANSFER_FAILED;
    asyncTransferProgress_ = 0;
  }
}

void FloppyManager::updateAsyncTransfer() {
  // TODO: Async transfer not yet fully implemented
  // XModemFileReceiver is currently blocking-only
  // Future enhancement: Make XModem non-blocking for async transfers

  // For now, this is a stub - async transfers are not supported
  // Use blocking transferFile() instead
}
