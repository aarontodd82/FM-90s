#include "xmodem_wrapper.h"

// Static instance pointer
XModemFileReceiver* XModemFileReceiver::instance_ = nullptr;

XModemFileReceiver::XModemFileReceiver(Stream* serial)
  : serial_(serial)
  , bytesReceived_(0)
  , errorMsg_(nullptr) {
  instance_ = this;
}

int XModemFileReceiver::recvChar(int msDelay) {
  if (!instance_ || !instance_->serial_) return -1;

  unsigned long start = millis();
  while ((int)(millis() - start) < msDelay) {
    if (instance_->serial_->available()) {
      return (uint8_t)instance_->serial_->read();
    }
  }
  return -1;
}

void XModemFileReceiver::sendData(const char* data, int len) {
  if (!instance_ || !instance_->serial_) return;
  instance_->serial_->write((const uint8_t*)data, len);
  instance_->serial_->flush();
}

bool XModemFileReceiver::dataHandler(unsigned long blockNum, char* data, int len) {
  if (!instance_) return false;

  // Write data to file
  size_t written = instance_->file_.write((const uint8_t*)data, len);
  if (written != (size_t)len) {
    instance_->setError("Failed to write to SD card");
    return false;
  }

  instance_->bytesReceived_ += written;

  // Flush to SD card periodically
  if (blockNum % 10 == 0) {
    instance_->file_.flush();
  }

  return true;
}

bool XModemFileReceiver::receiveFile(const char* destPath) {
  // Remove existing file
  if (SD.exists(destPath)) {
    SD.remove(destPath);
  }

  // Open file for writing
  file_ = SD.open(destPath, FILE_WRITE);
  if (!file_) {
    setError("Failed to open destination file");
    return false;
  }

  bytesReceived_ = 0;
  errorMsg_ = nullptr;

  // // Serial.println("\n=== Starting XMODEM-CRC Transfer ===");
  // // Serial.println("Using standard XModem library (proven implementation)");

  // Create XModem receiver with our callbacks
  XModem xmodem(recvChar, sendData, dataHandler);

  // Perform the receive operation
  bool success = xmodem.receive();

  // Close file
  file_.flush();
  file_.close();

  if (success) {
    // // Serial.println("=== Transfer Complete ===");
    // // Serial.print("Total bytes received: ");
    // // Serial.println(bytesReceived_);
    return true;
  } else {
    if (!errorMsg_) {
      setError("XModem transfer failed");
    }
    // Clean up failed transfer
    if (SD.exists(destPath)) {
      SD.remove(destPath);
    }
    return false;
  }
}

void XModemFileReceiver::setError(const char* msg) {
  errorMsg_ = msg;
  // // Serial.print("XMODEM ERROR: ");
  // // Serial.println(msg);
}
