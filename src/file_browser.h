#pragma once
#include <Arduino.h>
#include <SD.h>
#include <vector>

class FileBrowser {
public:
  FileBrowser();

  // Scan SD card for MIDI files
  void scanForMidiFiles();

  // Get list of found files
  const std::vector<String>& getFileList() const { return midiFiles_; }
  size_t getFileCount() const { return midiFiles_.size(); }

  // Display file list to serial
  void displayFileList() const;

  // Get file by index
  const char* getFile(size_t index) const;

  // Check if a string is a MIDI file (by extension)
  static bool isMidiFile(const char* filename);

  // Floppy file management
  void addFloppyFiles(const std::vector<String>& files, const char* tempPath);
  void clearFloppyFiles();
  void displayFloppyFileList() const;
  const char* getFloppyFile(size_t index) const;
  size_t getFloppyFileCount() const { return floppyFiles_.size(); }

private:
  std::vector<String> midiFiles_;
  std::vector<String> floppyFiles_;
  String floppyTempPath_;

  // Recursive directory scan
  void scanDirectory(File dir, const char* path);
};