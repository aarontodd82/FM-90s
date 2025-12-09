#pragma once
#include <Arduino.h>
#include <SD.h>
#include "XModem.h"

// Wrapper class to use the standard XModem library for receiving files
class XModemFileReceiver {
public:
  XModemFileReceiver(Stream* serial);

  // Receive a file via XMODEM and save to SD card
  bool receiveFile(const char* destPath);

  // Get error message if receive failed
  const char* getErrorMessage() const { return errorMsg_; }

  uint32_t getBytesReceived() const { return bytesReceived_; }

private:
  Stream* serial_;
  File file_;
  uint32_t bytesReceived_;
  const char* errorMsg_;

  // Callback functions for XModem library
  static int recvChar(int msDelay);
  static void sendData(const char* data, int len);
  static bool dataHandler(unsigned long blockNum, char* data, int len);

  // Static instance pointer for callbacks
  static XModemFileReceiver* instance_;

  void setError(const char* msg);
};
