#include "file_source.h"

FileSource::FileSource()
  : source_(SD_CARD)
  , usbFilesystem_(nullptr) {
}

void FileSource::setSource(Source source, void* context) {
  source_ = source;

  if (source == USB_DRIVE) {
    usbFilesystem_ = static_cast<FS*>(context);
    if (!usbFilesystem_) {
      // // Serial.println("WARNING: USB_DRIVE source set but no filesystem provided!");
    }
  } else {
    usbFilesystem_ = nullptr;
  }
}

File FileSource::open(const char* filename, uint8_t mode) {
  if (!filename) {
    // // Serial.println("ERROR: FileSource::open() called with null filename!");
    return File();
  }

  switch (source_) {
    case SD_CARD:
      return SD.open(filename, mode);

    case USB_DRIVE:
      if (usbFilesystem_) {
        return usbFilesystem_->open(filename, mode);
      } else {
        // // Serial.println("ERROR: USB filesystem not available!");
        return File();
      }

    case FLOPPY_TEMP:
      // Floppy files are copied to SD's /TEMP directory
      // The filename should already include the /TEMP prefix from FloppyManager
      return SD.open(filename, mode);

    default:
      // // Serial.println("ERROR: Unknown file source!");
      return File();
  }
}

bool FileSource::exists(const char* filename) {
  if (!filename) {
    return false;
  }

  switch (source_) {
    case SD_CARD:
      return SD.exists(filename);

    case USB_DRIVE:
      if (usbFilesystem_) {
        return usbFilesystem_->exists(filename);
      }
      return false;

    case FLOPPY_TEMP:
      return SD.exists(filename);

    default:
      return false;
  }
}
