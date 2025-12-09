#pragma once
#include <Arduino.h>
#include <SD.h>
#include <FS.h>

// Abstraction for opening files from different sources (SD card, USB drive, etc.)
// This allows players to be agnostic about where files come from
class FileSource {
public:
  enum Source {
    SD_CARD,      // Internal SD card
    USB_DRIVE,    // USB flash drive
    FLOPPY_TEMP   // Temporary files from floppy (stored on SD)
  };

  FileSource();

  // Set the current file source
  // For USB_DRIVE: context should be a FS* pointer to the USB filesystem
  // For SD_CARD and FLOPPY_TEMP: context should be nullptr
  void setSource(Source source, void* context = nullptr);

  // Get current source type
  Source getSource() const { return source_; }

  // Open a file from the current source
  // Returns a File object that can be used with standard SD library operations
  File open(const char* filename, uint8_t mode = FILE_READ);

  // Check if a file exists in the current source
  bool exists(const char* filename);

private:
  Source source_;
  FS* usbFilesystem_;  // Pointer to USB filesystem (only valid when source_ == USB_DRIVE)
};
