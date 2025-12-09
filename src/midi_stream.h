#pragma once
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <cstdint>
#include "midi_common.h"  // MidiEvent and MidiEventType definitions
#include "file_source.h"  // File source abstraction

// Forward declaration
class StreamingMidiSong;

/**
 * TrackStream - Streams events from a single MIDI track
 *
 * Maintains a small lookahead buffer of events read from storage.
 * File position is tracked per-track, allowing independent streaming.
 */
class TrackStream {
public:
  TrackStream();
  ~TrackStream();

  // Initialize stream with file and track boundaries
  // file: File handle (each track gets its own handle to the same file)
  // startPos: Byte offset where track data begins (after MTrk + length)
  // length: Track data length in bytes
  bool begin(File& file, uint32_t startPos, uint32_t length);

  // Event access (same as MidiSong interface)
  bool peek(MidiEvent& out);  // View next event without consuming
  bool pop(MidiEvent& out);   // Consume next event
  bool isDone() const { return eof_ && bufferSize_ == 0; }

  // For debugging
  uint32_t getCurrentTick() const { return nextEventTick_; }
  uint32_t getBytesRead() const { return currentFilePos_ - trackStartPos_; }
  uint32_t getTotalBytes() const { return trackEndPos_ - trackStartPos_; }

private:
  // Refill buffer with events from file
  bool refillBuffer();

  // Parse next event from file into buffer
  bool parseNextEvent();

  // MIDI parser helpers (shared with midi_file.cpp logic)
  static uint32_t readBE32(const uint8_t* p);
  static uint16_t readBE16(const uint8_t* p);
  bool readVarLen(uint32_t& out);  // Reads from file, not buffer
  bool readByte(uint8_t& out);     // Read single byte from file

  // File state
  File file_;                  // File handle for this track
  uint32_t trackStartPos_;     // Byte offset where track data starts
  uint32_t trackEndPos_;       // Byte offset where track ends
  uint32_t currentFilePos_;    // Current read position in file
  bool eof_;                   // Track data exhausted

  // Parser state
  uint32_t absoluteTick_;      // Current absolute tick (accumulated deltas)
  uint8_t runningStatus_;      // MIDI running status byte

  // Event buffer (ring buffer)
  static const uint8_t BUFFER_SIZE = 32;
  MidiEvent buffer_[BUFFER_SIZE];
  uint8_t bufferHead_;         // Next slot to write
  uint8_t bufferTail_;         // Next slot to read
  uint8_t bufferSize_;         // Number of events in buffer

  // Next event tracking (for peek/pop)
  uint32_t nextEventTick_;     // Tick time of next event to return
};

/**
 * StreamingMidiSong - Streams MIDI events from multi-track SMF files
 *
 * Merges events from multiple track streams in real-time.
 * Memory usage is O(num_tracks × buffer_size) instead of O(total_events).
 *
 * Interface is identical to MidiSong for drop-in replacement.
 */
class StreamingMidiSong {
public:
  StreamingMidiSong();
  ~StreamingMidiSong();

  // Load and parse MIDI file header, setup track streams
  // filename: Path to the MIDI file
  // fileSource: FileSource object to use for opening track file handles
  bool loadFromFile(const char* filename, FileSource* fileSource);

  // Playback interface (identical to MidiSong)
  bool peekEvent(MidiEvent& out);  // Peek at next event across all tracks
  bool popEvent(MidiEvent& out);   // Pop next event across all tracks
  bool playbackDone(uint32_t lastTickDispatched) const;

  // Timing (identical to MidiSong)
  uint16_t ppqn() const { return ppqn_; }
  uint32_t initialTempoUSQ() const { return initialTempoUSQ_; }
  uint32_t usPerTick() const { return currentUSPerTick_; }
  void applyTempoChange(uint32_t tempoUSQ);

  // State management
  void clear();         // Close all streams and reset
  void resetPlayback(); // NOT SUPPORTED for streaming (would require file reopen)

private:
  // Find track with earliest event
  int findEarliestTrack(MidiEvent& out);

  // Track streams
  TrackStream* tracks_;     // Array of track streams
  uint8_t numTracks_;       // Number of tracks
  uint8_t maxTracks_;       // Allocated track array size

  // MIDI file properties
  uint16_t ppqn_;
  uint32_t initialTempoUSQ_;
  uint32_t currentUSPerTick_;

  // Format info
  uint16_t format_;         // SMF format (0 or 1)

  // File source
  FileSource* fileSource_;  // File source abstraction
  char filename_[64];       // Current filename
};
