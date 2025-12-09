#pragma once
#include <Arduino.h>
#include <SD.h>
#include <cstdint>
#include "../lib/uzlib/uzlib.h"

// Forward declarations
class FileSource;
class GenesisBoard;

// VGM file header structure (version 1.71)
struct VGMHeader {
  char ident[4];           // "Vgm " identifier
  uint32_t eofOffset;      // Relative offset to end of file
  uint32_t version;        // Version number
  uint32_t sn76489Clock;   // PSG clock
  uint32_t ym2413Clock;    // OPLL clock
  uint32_t gd3Offset;      // Relative offset to GD3 tag
  uint32_t totalSamples;   // Total # samples
  uint32_t loopOffset;     // Relative offset to loop point
  uint32_t loopSamples;    // # samples in one loop
  uint32_t rate;           // Rate (typically ignored, assumed 60Hz)
  uint16_t sn76489Feedback; // SN76489 feedback
  uint8_t sn76489ShiftReg; // SN76489 shift register width
  uint8_t sn76489Flags;    // SN76489 flags
  uint32_t ym2612Clock;    // YM2612 clock
  uint32_t ym2151Clock;    // YM2151 clock
  uint32_t vgmDataOffset;  // Relative offset to VGM data (0x34 if version < 1.50)
  uint32_t segaPCMClock;   // Sega PCM clock
  uint32_t spcmInterface;  // SPCM interface
  uint32_t rf5c68Clock;    // RF5C68 clock
  uint32_t ym2203Clock;    // YM2203 clock
  uint32_t ym2608Clock;    // YM2608 clock
  uint32_t ym2610Clock;    // YM2610/B clock
  uint32_t ym3812Clock;    // YM3812 (OPL2) clock - offset 0x50
  uint32_t ym3526Clock;    // YM3526 (OPL) clock
  uint32_t y8950Clock;     // Y8950 clock
  uint32_t ymf262Clock;    // YMF262 (OPL3) clock - offset 0x5C
  uint32_t ymf278bClock;   // YMF278B clock
  uint32_t ymf271Clock;    // YMF271 clock
  uint32_t ymz280bClock;   // YMZ280B clock
  uint32_t rf5c164Clock;   // RF5C164 clock
  uint32_t pwmClock;       // PWM clock
  uint32_t ay8910Clock;    // AY8910 clock
  uint8_t ay8910Type;      // AY8910 chip type
  uint8_t ay8910Flags;     // AY8910 flags
  uint8_t ym2203Flags;     // YM2203 flags
  uint8_t ym2608Flags;     // YM2608 flags
  uint32_t volumeModifier; // Volume modifier - offset 0x7C
  uint32_t gbDMGClock;     // Game Boy DMG clock - offset 0x80
  uint32_t nesAPUClock;    // NES APU clock (RP2A03) - offset 0x84
  // ... more fields in newer versions
};

enum class ChipType {
  NONE,
  YM3812_OPL2,
  YMF262_OPL3,
  DUAL_OPL2,
  DUAL_OPL3,
  NES_APU,
  GAMEBOY_DMG,
  SEGA_GENESIS,    // YM2612 + SN76489 combo
  YM2612_ONLY,     // YM2612 FM chip alone
  SN76489_ONLY     // SN76489 PSG chip alone
};

class VGMFile {
public:
  VGMFile();
  ~VGMFile();

  // Load VGM or VGZ file for streaming
  bool loadFromFile(const char* filename, FileSource* fileSource);

  // File information
  ChipType getChipType() const { return chipType_; }
  uint32_t getTotalSamples() const { return header_.totalSamples; }
  uint32_t getSampleRate() const { return 44100; }  // VGM standard
  bool hasLoop() const { return header_.loopOffset > 0; }
  uint32_t getLoopSamples() const { return header_.loopSamples; }
  uint32_t getLoopOffset() const { return header_.loopOffset; }

  // Get loop point position in samples (where to jump back to when looping)
  // VGM structure: [intro section] + [loop section]
  // Loop point = totalSamples - loopSamples (NOT the beginning of the file!)
  uint32_t getLoopPointSample() const {
    if (!hasLoop()) return 0;
    return header_.totalSamples - header_.loopSamples;
  }

  // Get version string
  String getVersionString() const;

  // Streaming data access
  size_t getDataSize() const { return vgmDataSize_; }
  uint32_t getDataOffset() const { return dataOffset_; }
  uint32_t getLoopOffsetInData() const { return loopOffsetInData_; }

  // Read next byte from stream (handles buffer refill)
  bool readByte(uint8_t& byte);

  // Peek at next byte without advancing
  bool peekByte(uint8_t& byte);

  // Seek to position in data stream (relative to data start)
  bool seekToDataPosition(uint32_t position);

  // Get current position in data stream
  uint32_t getCurrentDataPosition() const { return currentDataPos_; }

  // Check if at end of data
  bool isAtEnd() const { return endOfData_ || currentDataPos_ >= vgmDataSize_; }

  // Explicitly mark end of data (for VGZ/FM9 files where size is unknown)
  void markEndOfData() { endOfData_ = true; }

  // Clear loaded data and close file
  void clear();

  // ========== Data Bank Support (for PCM samples) ==========

  /**
   * Read next byte from PCM data bank
   * Used by commands 0x80-0x8F for YM2612 DAC streaming
   * @param byte Output: byte read from data bank
   * @return true if byte was read successfully
   */
  bool readDataBankByte(uint8_t& byte);

  /**
   * Seek to position in PCM data bank
   * Used by command 0xE0
   * @param offset Absolute offset in data bank
   */
  void seekDataBank(uint32_t offset);

  /**
   * Get current data bank position
   * @return Current read position in data bank
   */
  uint32_t getDataBankPosition() const { return dataBankPos_; }

  /**
   * Get total data bank size
   * @return Size of PCM data bank in bytes
   */
  uint32_t getDataBankSize() const { return dataBankSize_; }

  /**
   * Get direct pointer to data bank (for pre-rendering)
   * WARNING: This returns the raw pointer - do not free or modify!
   * @return Pointer to PCM data bank, or nullptr if no data
   */
  const uint8_t* getDataBankPtr() const { return dataBank_; }

  /**
   * Append PCM data to data bank
   * Used by command 0x67 to load PCM samples
   * @param data Pointer to PCM data
   * @param size Size of data in bytes
   */
  void appendToDataBank(const uint8_t* data, uint32_t size);

  // ========== Stream Control Support (commands 0x90-0x95) ==========

  /**
   * Setup stream control (command 0x90)
   * @param streamID Stream identifier (0-3)
   * @param chipType Chip type (0x02 = YM2612)
   * @param port Port to write to
   * @param command Register/command to write
   */
  void setupStream(uint8_t streamID, uint8_t chipType, uint8_t port, uint8_t command);

  /**
   * Set stream data parameters (command 0x91)
   * @param streamID Stream identifier (0-3)
   * @param dataBankID Data bank ID (usually 0)
   * @param stepSize Step size (bytes to skip after each read)
   * @param stepBase Step base (unused, for compatibility)
   */
  void setStreamData(uint8_t streamID, uint8_t dataBankID, uint8_t stepSize, uint8_t stepBase);

  /**
   * Set stream frequency (command 0x92)
   * @param streamID Stream identifier (0-3)
   * @param frequency Sample rate in Hz
   */
  void setStreamFrequency(uint8_t streamID, uint32_t frequency);

  /**
   * Start stream playback (command 0x93)
   * @param streamID Stream identifier (0-3)
   * @param dataStart Start offset in data bank
   * @param lengthMode Length mode (0=length in bytes, 1=length in samples)
   * @param dataLength Length of data
   */
  void startStream(uint8_t streamID, uint32_t dataStart, uint8_t lengthMode, uint32_t dataLength);

  /**
   * Stop stream playback (command 0x94)
   * @param streamID Stream identifier (0-3)
   */
  void stopStream(uint8_t streamID);

  /**
   * Start stream with fast call (command 0x95)
   * Combines setup, data, frequency, and start into one command
   * @param streamID Stream identifier (0-3)
   * @param blockID Pre-configured block ID
   * @param flags Control flags
   */
  void startStreamFast(uint8_t streamID, uint16_t blockID, uint8_t flags);

  /**
   * Update all active streams (called from VGMPlayer::update())
   * Writes next sample to chip when timing interval elapses
   * Only used for hardware DAC mode - pre-rendered DAC handles streams internally
   * @param genesisBoard Hardware DAC interface
   */
  void updateStreams(class GenesisBoard* genesisBoard);

  /**
   * Reset all stream positions to start (for looping)
   * Called when VGM loops back to loop point
   */
  void resetStreamPositions();

private:
  static const size_t BUFFER_SIZE = 8192;  // 8KB buffer for streaming
  static const size_t COMPRESSED_BUFFER_SIZE = 4096;  // 4KB for compressed input
  static const size_t DICT_SIZE = 32768;  // 32KB LZ77 dictionary for gzip

  // File mode
  enum FileMode {
    MODE_UNCOMPRESSED,  // .vgm - direct streaming
    MODE_COMPRESSED     // .vgz - streaming decompression
  };

  // Loop snapshot for VGZ files (captures decompressor state at loop point)
  struct LoopSnapshot {
    uint32_t compressedFilePos;      // Position in compressed file
    uint32_t decompressedDataPos;    // Position in decompressed stream
    size_t compressedBufferOffset;   // Offset into compressed buffer where decompressor was
    uzlib_uncomp decompressorState;  // Full decompressor state
    uint8_t* dictCopy;               // Copy of LZ77 dictionary
    size_t dictSize;                 // Dictionary size
    uint8_t* savedBufferData;        // Saved decompressed buffer data from loop point
    size_t savedBufferSize;          // Size of saved buffer data
    bool valid;                      // Snapshot is ready
  };

  VGMHeader header_;
  ChipType chipType_;

  // File streaming
  File file_;
  bool isTempFile_;                // True if file is a temp file (needs deletion)
  char tempFileName_[32];          // Store temp filename for cleanup
  uint8_t buffer_[BUFFER_SIZE];    // Streaming buffer (decompressed data)
  size_t bufferPos_;               // Current position in buffer
  size_t bufferSize_;              // Number of valid bytes in buffer
  uint32_t fileDataStartOffset_;   // Offset in file where VGM data starts
  FileMode fileMode_;              // File compression mode

  // VGZ streaming decompression
  uint8_t* compressedBuffer_;      // Buffer for compressed input
  uint8_t* streamDictBuffer_;      // LZ77 sliding window dictionary
  uzlib_uncomp decompressor_;      // Decompressor state
  bool decompressorActive_;        // Is decompressor initialized?
  LoopSnapshot loopSnapshot_;      // Saved state at loop point

  // VGM data tracking
  size_t vgmDataSize_;             // Size of VGM command data
  uint32_t dataOffset_;            // Offset to VGM data start in file
  uint32_t currentDataPos_;        // Current position in data stream (relative to data start)
  uint32_t loopOffsetInData_;      // Loop position relative to data start
  bool endOfData_;                 // Explicit end flag (for 0x66 command)

  // File source
  FileSource* fileSource_;         // File source abstraction

  // PCM Data Bank (for YM2612 DAC samples)
  // Stored in PSRAM to conserve internal RAM
  static const size_t MAX_DATA_BANK_SIZE = 262144;  // 256KB max for PCM data (we have 8MB PSRAM!)
  uint8_t* dataBank_;              // PCM sample data storage (allocated in PSRAM)
  uint32_t dataBankSize_;          // Size of loaded data bank
  uint32_t dataBankPos_;           // Current read position in data bank

  // Stream Control (commands 0x90-0x95)
  struct StreamState {
    bool active;              // Is stream currently active?
    uint8_t chipType;         // Chip type (YM2612 = 0x02)
    uint8_t port;             // Port to write to
    uint8_t command;          // Command/register to write
    uint8_t dataBankID;       // Which data bank to use
    uint8_t stepSize;         // Bytes to skip after each read
    uint32_t frequency;       // Sample rate in Hz
    uint32_t dataStart;       // Start offset in data bank
    uint32_t dataLength;      // Length of data
    uint32_t dataPos;         // Current position in stream data
    bool loop;                // Loop when reaching end?
    uint32_t nextUpdateTime;  // When to write next sample (micros)
  };
  static const int MAX_STREAMS = 4;  // Support up to 4 concurrent streams
  StreamState streams_[MAX_STREAMS];

  // Helper methods
  bool loadVGZ(const char* filename);
  bool loadVGZStreaming(const char* filename);  // New streaming version
  bool decompressVGZToTemp(const char* filename);  // Old fallback method
  bool loadVGM(const char* filename);
  bool parseHeader();
  ChipType detectChipType();
  bool refillBuffer();
  bool refillBufferCompressed();  // Decompress next chunk
  void captureLoopSnapshot();     // Save decompressor state at loop point
  bool restoreLoopSnapshot();     // Restore decompressor state for looping
  static int streamingReadCallback(uzlib_uncomp* uncomp);  // Callback for uzlib
  uint32_t readLE32(const uint8_t* p);
  uint16_t readLE16(const uint8_t* p);

  // Data bank helpers (uses PSRAM allocation)
  bool allocateDataBank();
  void clearDataBank();
};