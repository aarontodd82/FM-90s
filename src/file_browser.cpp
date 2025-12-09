#include "file_browser.h"

FileBrowser::FileBrowser() {
  midiFiles_.reserve(100); // Reserve space for up to 100 files
}

void FileBrowser::scanForMidiFiles() {
  midiFiles_.clear();

  // // Serial.println("Scanning SD card for music files...");

  File root = SD.open("/");
  if (!root) {
    // // Serial.println("Failed to open SD card root");
    return;
  }

  if (!root.isDirectory()) {
    // // Serial.println("Root is not a directory");
    root.close();
    return;
  }

  scanDirectory(root, "/");
  root.close();

  // // Serial.print("Found ");
  // // Serial.print(midiFiles_.size());
  // // Serial.println(" music files");
}

void FileBrowser::scanDirectory(File dir, const char* path) {
  // Limit recursion depth to prevent stack overflow
  static int depth = 0;
  if (depth > 5) return;
  depth++;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break; // No more files

    if (entry.isDirectory()) {
      // Recursively scan subdirectories
      // Build path for subdirectory
      String subPath = String(path);
      if (!subPath.endsWith("/")) subPath += "/";
      subPath += entry.name();

      scanDirectory(entry, subPath.c_str());
    } else {
      // Check if it's a valid music file
      if (isMidiFile(entry.name())) {
        String fullPath = String(path);
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += entry.name();

        // Remove leading slash if present for cleaner display
        if (fullPath.startsWith("/")) {
          fullPath = fullPath.substring(1);
        }

        midiFiles_.push_back(fullPath);

        // Show progress for large scans
        if (midiFiles_.size() % 10 == 0) {
          // // Serial.print(".");
        }
      }
    }
    entry.close();
  }
  depth--;
}

bool FileBrowser::isMidiFile(const char* filename) {
  if (!filename) return false;

  // Convert to lowercase for comparison
  String name = String(filename);
  name.toLowerCase();

  // Check for supported music file extensions
  return name.endsWith(".mid") ||
         name.endsWith(".midi") ||
         name.endsWith(".smf") ||
         name.endsWith(".kar") ||   // Karaoke MIDI files
         name.endsWith(".vgm") ||   // VGM files
         name.endsWith(".vgz") ||   // Compressed VGM files
         name.endsWith(".fm9") ||   // FM9 extended VGM files (VGM + audio + FX)
         name.endsWith(".spc") ||   // SNES SPC files
         name.endsWith(".mod") ||   // Protracker MOD files
         name.endsWith(".s3m") ||   // Scream Tracker 3 files
         name.endsWith(".xm") ||    // FastTracker II files
         name.endsWith(".it");      // Impulse Tracker files
}

void FileBrowser::displayFileList() const {
  if (midiFiles_.empty()) {
    // // Serial.println("No music files found on SD card");
    return;
  }

  // // Serial.println("\n=== Music Files on SD Card ===");
  for (size_t i = 0; i < midiFiles_.size(); i++) {
    // // Serial.print(i + 1);
    // // Serial.print(". ");
    // // Serial.println(midiFiles_[i]);
  }
  // // Serial.println("=============================");
}

const char* FileBrowser::getFile(size_t index) const {
  if (index >= midiFiles_.size()) {
    return nullptr;
  }
  return midiFiles_[index].c_str();
}

// Floppy file management functions
void FileBrowser::addFloppyFiles(const std::vector<String>& files, const char* tempPath) {
  floppyFiles_.clear();
  floppyTempPath_ = String(tempPath);

  // Build full paths for floppy files
  for (const String& file : files) {
    String fullPath = floppyTempPath_;
    if (!fullPath.endsWith("/")) fullPath += "/";
    fullPath += file;
    floppyFiles_.push_back(fullPath);
  }

  // // Serial.print("Added ");
  // // Serial.print(floppyFiles_.size());
  // // Serial.println(" floppy files to browser");
}

void FileBrowser::clearFloppyFiles() {
  floppyFiles_.clear();
  floppyTempPath_ = "";
}

void FileBrowser::displayFloppyFileList() const {
  if (floppyFiles_.empty()) {
    // // Serial.println("No music files from floppy disk");
    return;
  }

  // // Serial.println("\n=== Music Files from Floppy ===");
  for (size_t i = 0; i < floppyFiles_.size(); i++) {
    // // Serial.print(i + 1);
    // // Serial.print(". ");
    // Display just the filename, not the full temp path
    String filename = floppyFiles_[i];
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
      filename = filename.substring(lastSlash + 1);
    }
    // // Serial.println(filename);
  }
  // // Serial.println("===============================");
}

const char* FileBrowser::getFloppyFile(size_t index) const {
  if (index >= floppyFiles_.size()) {
    return nullptr;
  }
  return floppyFiles_[index].c_str();
}