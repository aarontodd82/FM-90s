#include "midi_stream.h"

// ============================================================================
// TrackStream Implementation
// ============================================================================

TrackStream::TrackStream()
  : trackStartPos_(0)
  , trackEndPos_(0)
  , currentFilePos_(0)
  , eof_(false)
  , absoluteTick_(0)
  , runningStatus_(0)
  , bufferHead_(0)
  , bufferTail_(0)
  , bufferSize_(0)
  , nextEventTick_(0) {
}

TrackStream::~TrackStream() {
  if (file_) {
    file_.close();
  }
}

bool TrackStream::begin(File& file, uint32_t startPos, uint32_t length) {
  if (!file) {
    // // Serial.println("TrackStream::begin - invalid file");
    return false;
  }

  // Store file handle (each track gets its own handle)
  file_ = file;
  trackStartPos_ = startPos;
  trackEndPos_ = startPos + length;
  currentFilePos_ = startPos;
  eof_ = false;
  absoluteTick_ = 0;
  runningStatus_ = 0;
  bufferHead_ = 0;
  bufferTail_ = 0;
  bufferSize_ = 0;
  nextEventTick_ = 0;

  // Seek to track start
  if (!file_.seek(trackStartPos_)) {
    // // Serial.println("TrackStream::begin - seek failed");
    return false;
  }

  // Fill initial buffer
  return refillBuffer();
}

bool TrackStream::peek(MidiEvent& out) {
  // If buffer empty, try to refill
  if (bufferSize_ == 0) {
    if (eof_) {
      return false;  // Track exhausted
    }
    if (!refillBuffer()) {
      return false;  // Refill failed
    }
  }

  // Return event at tail without consuming
  if (bufferSize_ > 0) {
    out = buffer_[bufferTail_];
    return true;
  }

  return false;
}

bool TrackStream::pop(MidiEvent& out) {
  // Peek first
  if (!peek(out)) {
    return false;
  }

  // Consume event
  bufferTail_ = (bufferTail_ + 1) % BUFFER_SIZE;
  bufferSize_--;

  // Update next event tick
  if (bufferSize_ > 0) {
    nextEventTick_ = buffer_[bufferTail_].tick;
  }

  return true;
}

bool TrackStream::refillBuffer() {
  // Try to fill buffer to capacity
  while (bufferSize_ < BUFFER_SIZE && !eof_) {
    if (!parseNextEvent()) {
      // Either EOF or parse error
      break;
    }
  }

  return bufferSize_ > 0;
}

bool TrackStream::readByte(uint8_t& out) {
  if (currentFilePos_ >= trackEndPos_) {
    eof_ = true;
    return false;
  }

  int result = file_.read();
  if (result < 0) {
    // // Serial.println("TrackStream: file read error");
    eof_ = true;
    return false;
  }

  out = (uint8_t)result;
  currentFilePos_++;
  return true;
}

bool TrackStream::readVarLen(uint32_t& out) {
  out = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t c;
    if (!readByte(c)) {
      return false;
    }
    out = (out << 7) | (c & 0x7F);
    if (!(c & 0x80)) {
      return true;  // Done
    }
  }
  // VarLen too long (>4 bytes)
  return false;
}

uint32_t TrackStream::readBE32(const uint8_t* p) {
  return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

uint16_t TrackStream::readBE16(const uint8_t* p) {
  return (uint16_t)p[0]<<8 | p[1];
}

bool TrackStream::parseNextEvent() {
  if (currentFilePos_ >= trackEndPos_) {
    eof_ = true;
    return false;
  }

  // Read delta time
  uint32_t delta = 0;
  if (!readVarLen(delta)) {
    eof_ = true;
    return false;
  }
  absoluteTick_ += delta;

  // Read status byte
  uint8_t status;
  if (!readByte(status)) {
    eof_ = true;
    return false;
  }

  // Handle running status
  if (status < 0x80) {
    // Running status - rewind one byte
    if (!runningStatus_) {
      // // Serial.println("TrackStream: No running status for data byte");
      eof_ = true;
      return false;
    }
    currentFilePos_--;  // Put byte back
    file_.seek(currentFilePos_);
    status = runningStatus_;
  } else if ((status & 0xF0) != 0xF0) {
    // Update running status (only for channel voice messages)
    runningStatus_ = status;
  }

  // Parse event based on status
  if ((status & 0xF0) == 0xF0) {
    // System / Meta event
  if (status == 0xFF) {
    // Meta event
    uint8_t type;
    if (!readByte(type)) {
      eof_ = true;
      return false;
    }

    uint32_t len = 0;
    if (!readVarLen(len)) {
      eof_ = true;
      return false;
    }

    // Bounds check
    if (currentFilePos_ + len > trackEndPos_) {
      // // Serial.println("TrackStream: Meta event exceeds track bounds");
      eof_ = true;
      return false;
    }

    // Handle specific meta events
    if (type == 0x2F) {
      // End of track
      MidiEvent e;
      e.tick = absoluteTick_;
      e.type = MidiEventType::EndOfTrack;
      e.channel = 0;
      buffer_[bufferHead_] = e;
      bufferHead_ = (bufferHead_ + 1) % BUFFER_SIZE;
      bufferSize_++;
      if (bufferSize_ == 1) {
        nextEventTick_ = e.tick;
      }

      // Skip length bytes (should be 0)
      for (uint32_t i = 0; i < len; i++) {
        uint8_t dummy;
        readByte(dummy);
      }

      eof_ = true;  // Mark end of track
      return true;

    } else if (type == 0x51 && len == 3) {
      // Tempo change
      uint8_t data[3];
      for (int i = 0; i < 3; i++) {
        if (!readByte(data[i])) {
          eof_ = true;
          return false;
        }
      }

      uint32_t usq = ((uint32_t)data[0]<<16) | ((uint32_t)data[1]<<8) | data[2];

      MidiEvent e;
      e.tick = absoluteTick_;
      e.type = MidiEventType::MetaTempo;
      e.channel = 0;
      e.tempoUSQ = usq;
      buffer_[bufferHead_] = e;
      bufferHead_ = (bufferHead_ + 1) % BUFFER_SIZE;
      bufferSize_++;
      if (bufferSize_ == 1) {
        nextEventTick_ = e.tick;
      }

      return true;

    } else {
      // Skip unknown meta event
      for (uint32_t i = 0; i < len; i++) {
        uint8_t dummy;
        if (!readByte(dummy)) {
          eof_ = true;
          return false;
        }
      }
      return parseNextEvent();  // Recursively parse next event
    }

  } else {
    // SysEx (0xF0 or 0xF7)
    uint32_t len = 0;
    if (!readVarLen(len)) {
      eof_ = true;
      return false;
    }

    // Bounds check
    if (currentFilePos_ + len > trackEndPos_) {
      // // Serial.println("TrackStream: SysEx exceeds track bounds");
      eof_ = true;
      return false;
    }

    // Skip SysEx data
    for (uint32_t i = 0; i < len; i++) {
      uint8_t dummy;
      if (!readByte(dummy)) {
        eof_ = true;
        return false;
      }
    }

    return parseNextEvent();  // Recursively parse next event
  }
  } else {
    // Channel voice message
  uint8_t hi = status & 0xF0;
  uint8_t ch = status & 0x0F;

  MidiEvent e;
  e.tick = absoluteTick_;
  e.channel = ch;

  switch (hi) {
    case 0x80: {  // Note Off
      uint8_t key, vel;
      if (!readByte(key) || !readByte(vel)) {
        eof_ = true;
        return false;
      }
      e.type = MidiEventType::NoteOff;
      e.key = key;
      e.velocity = vel;
      break;
    }

    case 0x90: {  // Note On
      uint8_t key, vel;
      if (!readByte(key) || !readByte(vel)) {
        eof_ = true;
        return false;
      }
      e.type = (vel == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn;
      e.key = key;
      e.velocity = vel;
      break;
    }

    case 0xA0: {  // Poly Pressure (ignore)
      uint8_t dummy1, dummy2;
      if (!readByte(dummy1) || !readByte(dummy2)) {
        eof_ = true;
        return false;
      }
      return parseNextEvent();  // Skip this event, parse next
    }

    case 0xB0: {  // Control Change
      uint8_t cc, val;
      if (!readByte(cc) || !readByte(val)) {
        eof_ = true;
        return false;
      }
      e.type = MidiEventType::ControlChange;
      e.value1 = cc;
      e.value2 = val;
      break;
    }

    case 0xC0: {  // Program Change
      uint8_t prog;
      if (!readByte(prog)) {
        eof_ = true;
        return false;
      }
      e.type = MidiEventType::ProgramChange;
      e.value1 = prog;
      break;
    }

    case 0xD0: {  // Channel Pressure
      uint8_t pres;
      if (!readByte(pres)) {
        eof_ = true;
        return false;
      }
      e.type = MidiEventType::ChannelPressure;
      e.value1 = pres;
      break;
    }

    case 0xE0: {  // Pitch Bend
      uint8_t lsb, msb;
      if (!readByte(lsb) || !readByte(msb)) {
        eof_ = true;
        return false;
      }
      int16_t pb = ((int16_t)(msb & 0x7F) << 7) | (int16_t)(lsb & 0x7F);
      pb -= 8192;
      e.type = MidiEventType::PitchBend;
      e.pitchBend = pb;
      break;
    }

    default:
      // // Serial.print("TrackStream: Unknown status 0x");
      // // Serial.println(status, HEX);
      eof_ = true;
      return false;
  }

  // Add event to buffer
  buffer_[bufferHead_] = e;
  bufferHead_ = (bufferHead_ + 1) % BUFFER_SIZE;
  bufferSize_++;
  if (bufferSize_ == 1) {
    nextEventTick_ = e.tick;
  }

  return true;
  }
}

// ============================================================================
// StreamingMidiSong Implementation
// ============================================================================

StreamingMidiSong::StreamingMidiSong()
  : tracks_(nullptr)
  , numTracks_(0)
  , maxTracks_(0)
  , ppqn_(480)
  , initialTempoUSQ_(500000)
  , currentUSPerTick_(500000 / 480)
  , format_(0)
  , fileSource_(nullptr) {
  memset(filename_, 0, sizeof(filename_));
}

StreamingMidiSong::~StreamingMidiSong() {
  clear();
}

bool StreamingMidiSong::loadFromFile(const char* filename, FileSource* fileSource) {
  // Clear any existing state
  clear();

  if (!filename || !fileSource) {
    // // Serial.println("StreamingMidiSong: Invalid filename or fileSource");
    return false;
  }

  // Store filename and fileSource
  strncpy(filename_, filename, sizeof(filename_) - 1);
  filename_[sizeof(filename_) - 1] = '\0';
  fileSource_ = fileSource;

  // Open the file
  File file = fileSource_->open(filename_, FILE_READ);
  if (!file) {
    // // Serial.print("StreamingMidiSong: Failed to open file: ");
    // // Serial.println(filename_);
    return false;
  }

  size_t fileSize = file.size();
  // // Serial.print("StreamingMidiSong: File size = ");
  // // Serial.println(fileSize);

  if (fileSize < 14) {
    // // Serial.println("StreamingMidiSong: File too small");
    file.close();
    return false;
  }

  // Read and parse header
  uint8_t headerBuf[14];
  file.seek(0);
  if (file.read(headerBuf, 14) != 14) {
    // // Serial.println("StreamingMidiSong: Failed to read header");
    file.close();
    return false;
  }

  // Check MThd signature
  if (memcmp(headerBuf, "MThd", 4) != 0) {
    // // Serial.println("StreamingMidiSong: Invalid header signature");
    return false;
  }

  uint32_t headerLen = ((uint32_t)headerBuf[4]<<24) | ((uint32_t)headerBuf[5]<<16) |
                       ((uint32_t)headerBuf[6]<<8) | headerBuf[7];

  if (headerLen < 6) {
    // // Serial.println("StreamingMidiSong: Invalid header length");
    return false;
  }

  format_ = ((uint16_t)headerBuf[8]<<8) | headerBuf[9];
  numTracks_ = ((uint16_t)headerBuf[10]<<8) | headerBuf[11];
  int16_t div = ((int16_t)headerBuf[12]<<8) | headerBuf[13];

  // Serial.print("Format: "); // Serial.println(format_);
  // Serial.print("Tracks: "); // Serial.println(numTracks_);
  // Serial.print("Division: "); // Serial.println(div);

  // Only support PPQN timing (not SMPTE)
  if (div < 0) {
    // // Serial.println("StreamingMidiSong: SMPTE timing not supported");
    return false;
  }
  ppqn_ = (uint16_t)div;

  // Skip rest of header if needed
  uint32_t headerEndPos = 8 + headerLen;
  file.seek(headerEndPos);

  // Allocate track array
  maxTracks_ = numTracks_;
  tracks_ = new TrackStream[maxTracks_];
  if (!tracks_) {
    // // Serial.println("StreamingMidiSong: Failed to allocate track array");
    return false;
  }

  // Setup each track stream
  uint32_t filePos = headerEndPos;
  for (uint16_t t = 0; t < numTracks_; t++) {
    // Serial.print("Track "); Serial.print(t); // Serial.print("/"); // Serial.print(numTracks_);

    // Read MTrk header
    uint8_t trackHeader[8];
    file.seek(filePos);
    if (file.read(trackHeader, 8) != 8) {
      // // Serial.println(" - Failed to read track header");
      clear();
      return false;
    }

    // Check MTrk signature
    if (memcmp(trackHeader, "MTrk", 4) != 0) {
      // // Serial.println(" - Invalid track signature");
      clear();
      return false;
    }

    uint32_t trackLen = ((uint32_t)trackHeader[4]<<24) | ((uint32_t)trackHeader[5]<<16) |
                        ((uint32_t)trackHeader[6]<<8) | trackHeader[7];

    // Serial.print(" len="); // Serial.println(trackLen);

    // Bounds check
    if (filePos + 8 + trackLen > fileSize) {
      // // Serial.println(" - Track exceeds file size");
      clear();
      return false;
    }

    // Open a new file handle for this track using FileSource
    File trackFile = fileSource_->open(filename_, FILE_READ);
    if (!trackFile) {
      // // Serial.println(" - Failed to open file for track");
      clear();
      file.close();
      return false;
    }

    // Initialize track stream
    uint32_t trackDataStart = filePos + 8;
    if (!tracks_[t].begin(trackFile, trackDataStart, trackLen)) {
      // // Serial.println(" - Failed to initialize track stream");
      trackFile.close();
      clear();
      return false;
    }

    // Move to next track
    filePos += 8 + trackLen;
  }

  // // Serial.println("StreamingMidiSong: All tracks initialized successfully");

  // Close the main file handle - each track has its own handle now
  file.close();

  // Find initial tempo from first tempo event across all tracks
  initialTempoUSQ_ = 500000;  // Default 120 BPM
  MidiEvent ev;
  if (peekEvent(ev) && ev.type == MidiEventType::MetaTempo) {
    initialTempoUSQ_ = ev.tempoUSQ;
  }

  currentUSPerTick_ = (uint32_t)((double)initialTempoUSQ_ / (double)ppqn_);

  return true;
}

int StreamingMidiSong::findEarliestTrack(MidiEvent& out) {
  uint32_t earliestTick = UINT32_MAX;
  int earliestTrack = -1;

  // Scan all tracks for earliest event
  for (int i = 0; i < numTracks_; i++) {
    MidiEvent ev;
    if (tracks_[i].peek(ev)) {
      if (ev.tick < earliestTick) {
        earliestTick = ev.tick;
        earliestTrack = i;
        out = ev;
      }
    }
  }

  return earliestTrack;
}

bool StreamingMidiSong::peekEvent(MidiEvent& out) {
  return findEarliestTrack(out) >= 0;
}

bool StreamingMidiSong::popEvent(MidiEvent& out) {
  int track = findEarliestTrack(out);
  if (track < 0) {
    return false;
  }

  // Pop from that track
  return tracks_[track].pop(out);
}

bool StreamingMidiSong::playbackDone(uint32_t lastTickDispatched) const {
  // Playback done when all tracks are done
  for (int i = 0; i < numTracks_; i++) {
    if (!tracks_[i].isDone()) {
      return false;
    }
  }
  return true;
}

void StreamingMidiSong::applyTempoChange(uint32_t tempoUSQ) {
  initialTempoUSQ_ = tempoUSQ;
  currentUSPerTick_ = (uint32_t)((double)tempoUSQ / (double)ppqn_);
}

void StreamingMidiSong::clear() {
  if (tracks_) {
    delete[] tracks_;
    tracks_ = nullptr;
  }
  numTracks_ = 0;
  maxTracks_ = 0;
  ppqn_ = 480;
  initialTempoUSQ_ = 500000;
  currentUSPerTick_ = 500000 / 480;
  format_ = 0;
  fileSource_ = nullptr;
  memset(filename_, 0, sizeof(filename_));
}

void StreamingMidiSong::resetPlayback() {
  // // Serial.println("WARNING: StreamingMidiSong::resetPlayback() not supported");
  // // Serial.println("Streaming implementation requires reloading file to restart playback");
  // Could potentially re-seek all track file handles, but complex to reset parser state
}
