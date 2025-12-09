#include "vgm_file.h"
#include "file_source.h"
#include "genesis_board.h"
#include <Arduino.h>  // For extmem_malloc/extmem_free (PSRAM - Teensy core)
#include "../lib/uzlib/uzlib.h"
#include <string.h>

VGMFile::VGMFile()
  : chipType_(ChipType::NONE)
  , isTempFile_(false)
  , bufferPos_(0)
  , bufferSize_(0)
  , fileDataStartOffset_(0)
  , fileMode_(MODE_UNCOMPRESSED)
  , compressedBuffer_(nullptr)
  , streamDictBuffer_(nullptr)
  , decompressorActive_(false)
  , vgmDataSize_(0)
  , dataOffset_(0)
  , currentDataPos_(0)
  , loopOffsetInData_(0)
  , endOfData_(false)
  , fileSource_(nullptr)
  , dataBank_(nullptr)
  , dataBankSize_(0)
  , dataBankPos_(0) {
  memset(&header_, 0, sizeof(header_));
  memset(tempFileName_, 0, sizeof(tempFileName_));
  memset(&decompressor_, 0, sizeof(decompressor_));
  memset(&loopSnapshot_, 0, sizeof(loopSnapshot_));
  loopSnapshot_.valid = false;
  loopSnapshot_.dictCopy = nullptr;
  loopSnapshot_.savedBufferData = nullptr;

  // Initialize stream states
  for (int i = 0; i < MAX_STREAMS; i++) {
    streams_[i].active = false;
    streams_[i].chipType = 0;
    streams_[i].port = 0;
    streams_[i].command = 0;
    streams_[i].dataBankID = 0;
    streams_[i].stepSize = 0;
    streams_[i].frequency = 0;
    streams_[i].dataStart = 0;
    streams_[i].dataLength = 0;
    streams_[i].dataPos = 0;
    streams_[i].loop = false;
    streams_[i].nextUpdateTime = 0;
  }
}

VGMFile::~VGMFile() {
  clear();
}

void VGMFile::clear() {
  // Clear global streaming pointer if it's us
  extern VGMFile* g_streamingVGMFile;
  if (g_streamingVGMFile == this) {
    g_streamingVGMFile = nullptr;
  }

  // Close file if open
  if (file_) {
    file_.close();
  }

  // Delete temp file if it exists
  if (isTempFile_ && tempFileName_[0] != '\0') {
    SD.remove(tempFileName_);
  }

  // Free compressed streaming buffers
  if (compressedBuffer_) {
    delete[] compressedBuffer_;
    compressedBuffer_ = nullptr;
  }
  if (streamDictBuffer_) {
    delete[] streamDictBuffer_;
    streamDictBuffer_ = nullptr;
  }

  // Free loop snapshot dictionary
  if (loopSnapshot_.dictCopy) {
    delete[] loopSnapshot_.dictCopy;
    loopSnapshot_.dictCopy = nullptr;
  }

  // Free loop snapshot buffer data
  if (loopSnapshot_.savedBufferData) {
    delete[] loopSnapshot_.savedBufferData;
    loopSnapshot_.savedBufferData = nullptr;
  }

  // Free data bank (PSRAM)
  clearDataBank();

  // Reset all state
  chipType_ = ChipType::NONE;
  isTempFile_ = false;
  bufferPos_ = 0;
  bufferSize_ = 0;
  fileDataStartOffset_ = 0;
  fileMode_ = MODE_UNCOMPRESSED;
  decompressorActive_ = false;
  vgmDataSize_ = 0;
  dataOffset_ = 0;
  currentDataPos_ = 0;
  loopOffsetInData_ = 0;
  endOfData_ = false;
  fileSource_ = nullptr;
  memset(&header_, 0, sizeof(header_));
  memset(tempFileName_, 0, sizeof(tempFileName_));
  memset(&decompressor_, 0, sizeof(decompressor_));
  memset(&loopSnapshot_, 0, sizeof(loopSnapshot_));
  loopSnapshot_.valid = false;
}

bool VGMFile::loadFromFile(const char* filename, FileSource* fileSource) {
  clear();

  if (!filename || !fileSource) {
    // // Serial.println("VGMFile: Invalid filename or fileSource");
    return false;
  }

  // Store file source
  fileSource_ = fileSource;

  // Check file extension
  String fn = String(filename);
  fn.toLowerCase();

  if (fn.endsWith(".vgz") || fn.endsWith(".fm9")) {
    // FM9 files are gzip-compressed VGM with extensions appended after VGM data
    // VGMFile will decompress and stop at the VGM end marker (0x66)
    return loadVGZ(filename);
  } else if (fn.endsWith(".vgm")) {
    return loadVGM(filename);
  }

  // // Serial.println("Unknown file extension (expected .vgm or .vgz)");
  return false;
}

bool VGMFile::loadVGZ(const char* filename) {
  // // Serial.println("Loading VGZ (gzipped VGM) file...");

  // Try streaming decompression first (no temp file, minimal RAM)
  if (loadVGZStreaming(filename)) {
    // // Serial.println("VGZ loaded in streaming mode");
    return true;
  }

  // Fallback to old method (decompress to temp file)
  // // Serial.println("Streaming failed, trying temp file method...");
  if (!decompressVGZToTemp(filename)) {
    return false;
  }

  // Now load the temp VGM file
  bool result = loadVGM(tempFileName_);
  if (!result) {
    // Clean up temp file on failure
    SD.remove(tempFileName_);
    memset(tempFileName_, 0, sizeof(tempFileName_));
    isTempFile_ = false;
  }

  return result;
}

bool VGMFile::decompressVGZToTemp(const char* filename) {
  // Open VGZ file using FileSource
  File vgzFile = fileSource_->open(filename, FILE_READ);
  if (!vgzFile) {
    // // Serial.print("Could not open VGZ file: ");
    // // Serial.println(filename);
    return false;
  }

  size_t compressedSize = vgzFile.size();
  if (compressedSize < 18) {
    // // Serial.println("VGZ file too small");
    vgzFile.close();
    return false;
  }

  // // Serial.print("VGZ compressed size: ");
  // // Serial.print(compressedSize);
  // // Serial.println(" bytes");

  // Read entire compressed file
  uint8_t* compressedData = new uint8_t[compressedSize];
  if (!compressedData) {
    // // Serial.println("Failed to allocate buffer for compressed data");
    vgzFile.close();
    return false;
  }

  size_t bytesRead = vgzFile.read(compressedData, compressedSize);
  if (bytesRead != compressedSize) {
    // // Serial.println("Failed to read VGZ file");
    delete[] compressedData;
    vgzFile.close();
    return false;
  }
  vgzFile.close();

  // Get uncompressed size from gzip trailer
  uint32_t uncompressedSize = compressedData[compressedSize - 4] |
                              (compressedData[compressedSize - 3] << 8) |
                              (compressedData[compressedSize - 2] << 16) |
                              (compressedData[compressedSize - 1] << 24);

  // // Serial.print("VGZ uncompressed size: ");
  // // Serial.print(uncompressedSize);
  // // Serial.println(" bytes");

  // Try to allocate buffer for decompressed data
  // If RAM is insufficient, this will fail gracefully
  uint8_t* decompressedData = new uint8_t[uncompressedSize + 1024];
  if (!decompressedData) {
    // // Serial.print("ERROR: Not enough RAM to decompress ");
    // // Serial.print(uncompressedSize);
    // // Serial.println(" bytes");
    // // Serial.println("Try decompressing this .VGZ file to .VGM on your PC first.");
    // // Serial.println("(VGM files stream from SD card and have no size limit)");
    delete[] compressedData;
    return false;
  }

  // Initialize decompressor
  struct uzlib_uncomp d;
  memset(&d, 0, sizeof(d));
  uzlib_uncompress_init(&d, NULL, 0);

  // Set up source
  d.source = compressedData;
  d.source_limit = compressedData + compressedSize;
  d.source_read_cb = NULL;

  // Set up destination
  d.dest_start = decompressedData;
  d.dest = decompressedData;
  d.dest_limit = decompressedData + uncompressedSize + 1024;

  // Parse gzip header
  int res = uzlib_gzip_parse_header(&d);
  if (res != TINF_OK) {
    // // Serial.print("Failed to parse gzip header: ");
    // // Serial.println(res);
    delete[] compressedData;
    delete[] decompressedData;
    return false;
  }

  // Decompress
  // // Serial.print("Decompressing...");
  res = uzlib_uncompress(&d);

  if (res != TINF_DONE) {
    // // Serial.print("\nDecompression failed with code: ");
    // // Serial.println(res);
    delete[] compressedData;
    delete[] decompressedData;
    return false;
  }

  // Calculate actual decompressed size
  size_t actualSize = d.dest - decompressedData;
  // // Serial.print(" done! (");
  // // Serial.print(actualSize);
  // // Serial.println(" bytes)");

  // Free compressed data - don't need it anymore
  delete[] compressedData;

  // Create temp file name
  strncpy(tempFileName_, "~vgmtmp.vgm", sizeof(tempFileName_) - 1);
  isTempFile_ = true;

  // Create temp file - delete if it already exists
  if (SD.exists(tempFileName_)) {
    SD.remove(tempFileName_);
  }

  File tempFile = SD.open(tempFileName_, FILE_WRITE);
  if (!tempFile) {
    // // Serial.println("Failed to create temp file");
    delete[] decompressedData;
    return false;
  }

  // Write decompressed data to temp file
  // // Serial.print("Writing to temp file...");
  size_t written = tempFile.write(decompressedData, actualSize);
  tempFile.close();

  // Free decompressed data - don't need it anymore
  delete[] decompressedData;

  if (written != actualSize) {
    // // Serial.println(" failed!");
    return false;
  }

  // // Serial.println(" done!");
  return true;
}

// Static instance for streaming callback
static VGMFile* g_streamingVGMFile = nullptr;

int VGMFile::streamingReadCallback(uzlib_uncomp* uncomp) {
  if (!g_streamingVGMFile || !g_streamingVGMFile->file_) {
    return -1;  // EOF
  }

  // Check if we have buffered compressed data
  if (uncomp->source < uncomp->source_limit) {
    // Return next buffered byte
    return *uncomp->source++;
  }

  // Need to read more compressed data from file
  if (!g_streamingVGMFile->file_.available()) {
    return -1;  // EOF
  }

  int bytesRead = g_streamingVGMFile->file_.read(
    g_streamingVGMFile->compressedBuffer_,
    COMPRESSED_BUFFER_SIZE
  );

  if (bytesRead <= 0) {
    return -1;  // EOF or error
  }

  // Update source pointers
  uncomp->source = g_streamingVGMFile->compressedBuffer_;
  uncomp->source_limit = g_streamingVGMFile->compressedBuffer_ + bytesRead;

  // Return first byte
  return *uncomp->source++;
}

bool VGMFile::loadVGZStreaming(const char* filename) {
  // Set global instance for callback
  g_streamingVGMFile = this;
  // Open VGZ file using FileSource (keep it open for streaming)
  file_ = fileSource_->open(filename, FILE_READ);
  if (!file_) {
    // // Serial.print("Could not open VGZ file: ");
    // // Serial.println(filename);
    return false;
  }

  size_t compressedSize = file_.size();
  if (compressedSize < 18) {
    // // Serial.println("VGZ file too small");
    file_.close();
    return false;
  }

  // // Serial.print("VGZ compressed size: ");
  // // Serial.print(compressedSize);
  // // Serial.println(" bytes");

  // Allocate buffers for streaming
  compressedBuffer_ = new uint8_t[COMPRESSED_BUFFER_SIZE];
  if (!compressedBuffer_) {
    // // Serial.println("Failed to allocate compressed buffer");
    file_.close();
    return false;
  }

  streamDictBuffer_ = new uint8_t[DICT_SIZE];
  if (!streamDictBuffer_) {
    // // Serial.println("Failed to allocate dictionary buffer");
    delete[] compressedBuffer_;
    compressedBuffer_ = nullptr;
    file_.close();
    return false;
  }

  fileMode_ = MODE_COMPRESSED;

  // Initialize decompressor with dictionary
  memset(&decompressor_, 0, sizeof(decompressor_));
  uzlib_uncompress_init(&decompressor_, streamDictBuffer_, DICT_SIZE);

  // Read initial compressed data chunk
  size_t bytesRead = file_.read(compressedBuffer_, COMPRESSED_BUFFER_SIZE);
  if (bytesRead < 18) {
    // // Serial.println("Failed to read VGZ header");
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  // Set up source for decompressor with callback
  decompressor_.source = compressedBuffer_;
  decompressor_.source_limit = compressedBuffer_ + bytesRead;
  decompressor_.source_read_cb = streamingReadCallback;

  // Set up destination (use main buffer for decompressed output)
  decompressor_.dest_start = buffer_;
  decompressor_.dest = buffer_;
  decompressor_.dest_limit = buffer_ + BUFFER_SIZE;

  // Parse gzip header
  int res = uzlib_gzip_parse_header(&decompressor_);
  if (res != TINF_OK) {
    // // Serial.print("Failed to parse gzip header: ");
    // // Serial.println(res);
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  // // Serial.println("Gzip header parsed, starting decompression...");

  decompressorActive_ = true;

  // Decompress first chunk to get VGM header
  // The callback will automatically refill compressed data as needed
  while ((decompressor_.dest - buffer_) < (int)sizeof(VGMHeader)) {
    // Decompress next chunk
    res = uzlib_uncompress(&decompressor_);

    if (res != TINF_OK && res != TINF_DONE) {
      // // Serial.print("Failed to decompress initial data: ");
      // // Serial.println(res);
      delete[] compressedBuffer_;
      delete[] streamDictBuffer_;
      compressedBuffer_ = nullptr;
      streamDictBuffer_ = nullptr;
      file_.close();
      g_streamingVGMFile = nullptr;
      return false;
    }

    // If decompression is done, break
    if (res == TINF_DONE) {
      // // Serial.println("Decompression complete (reached end of stream)");
      break;
    }
  }

  // // Serial.print("Decompressed ");
  // // Serial.print(decompressor_.dest - buffer_);
  // // Serial.println(" bytes");

  // Calculate how much we decompressed
  bufferSize_ = decompressor_.dest - buffer_;
  bufferPos_ = 0;

  if (bufferSize_ < sizeof(VGMHeader)) {
    // // Serial.println("Decompressed data too small for VGM header");
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  // Read VGM header from decompressed buffer
  memcpy(&header_, buffer_, sizeof(VGMHeader));
  bufferPos_ = sizeof(VGMHeader);  // Skip past header

  // Check VGM signature
  if (memcmp(header_.ident, "Vgm ", 4) != 0) {
    // // Serial.println("Invalid VGM signature in decompressed data");
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  // Parse header and detect chip type
  if (!parseHeader()) {
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  chipType_ = detectChipType();
  if (chipType_ == ChipType::NONE) {
    // // Serial.println("VGM file does not contain supported chip data (OPL2/OPL3/Genesis/NES APU/Game Boy)");
    delete[] compressedBuffer_;
    delete[] streamDictBuffer_;
    compressedBuffer_ = nullptr;
    streamDictBuffer_ = nullptr;
    file_.close();
    return false;
  }

  // Calculate data offset within decompressed stream
  if (header_.version < 0x150) {
    dataOffset_ = 0x40;
  } else {
    dataOffset_ = 0x34 + header_.vgmDataOffset;
  }

  // We don't know the exact decompressed size for VGZ, use a large value
  // The 0x66 end-of-data command will stop playback
  vgmDataSize_ = 0xFFFFFFFF;  // Effectively unlimited
  fileDataStartOffset_ = 0;  // Not used for compressed mode

  // Calculate loop offset in decompressed data (if present)
  if (hasLoop()) {
    uint32_t loopOffsetInFile = 0x1C + header_.loopOffset;
    // // Serial.print("  Loop offset from header: 0x");
    // // Serial.print(header_.loopOffset, HEX);
    // // Serial.print(" (");
    // // Serial.print(header_.loopOffset);
    // // Serial.println(")");
    // // Serial.print("  Loop point in file: 0x");
    // // Serial.print(loopOffsetInFile, HEX);
    // // Serial.print(" (");
    // // Serial.print(loopOffsetInFile);
    // // Serial.println(")");
    // // Serial.print("  VGM data starts at: 0x");
    // // Serial.print(dataOffset_, HEX);
    // // Serial.print(" (");
    // // Serial.print(dataOffset_);
    // // Serial.println(")");

    if (loopOffsetInFile >= dataOffset_) {
      loopOffsetInData_ = loopOffsetInFile - dataOffset_;
      // // Serial.print("  Loop offset in data stream: ");
      // // Serial.println(loopOffsetInData_);
    } else {
      loopOffsetInData_ = 0;
      // // Serial.println("  WARNING: Loop offset is before data start!");
    }
  } else {
    loopOffsetInData_ = 0;
  }

  // Position ourselves at the start of VGM data
  // We need to skip from wherever we are in the buffer to dataOffset_
  if (bufferPos_ < dataOffset_) {
    // Need to skip ahead
    // // Serial.print("Skipping ");
    // // Serial.print(dataOffset_ - bufferPos_);
    // // Serial.println(" bytes to reach VGM data start");

    currentDataPos_ = bufferPos_;
    while (currentDataPos_ < dataOffset_) {
      uint8_t dummy;
      if (!readByte(dummy)) {
        // // Serial.println("Failed to skip to VGM data start");
        delete[] compressedBuffer_;
        delete[] streamDictBuffer_;
        compressedBuffer_ = nullptr;
        streamDictBuffer_ = nullptr;
        file_.close();
        g_streamingVGMFile = nullptr;
        return false;
      }
    }
  } else if (bufferPos_ > dataOffset_) {
    // We read too much (VGMHeader is larger than actual header)
    // Set bufferPos back to where VGM data actually starts
    // // Serial.print("VGMHeader struct is larger than actual header, adjusting bufferPos from ");
    // // Serial.print(bufferPos_);
    // // Serial.print(" to ");
    // // Serial.println(dataOffset_);
    bufferPos_ = dataOffset_;
  }

  // Now both bufferPos_ and currentDataPos_ should be at data start
  currentDataPos_ = 0;  // Reset to start of actual VGM data (relative to data start)

  // Initialize loop snapshot
  loopSnapshot_.valid = false;
  loopSnapshot_.dictCopy = nullptr;

  // Print info
  // // Serial.println("VGZ file loaded successfully (streaming mode):");
  // // Serial.print("  Version: ");
  // // Serial.println(getVersionString());
  // // Serial.print("  Chip: ");
  switch(chipType_) {
    case ChipType::YM3812_OPL2:
      // // Serial.println("YM3812 (OPL2)");
      break;
    case ChipType::YMF262_OPL3:
      // // Serial.println("YMF262 (OPL3)");
      break;
    case ChipType::DUAL_OPL2:
      // // Serial.println("Dual YM3812 (OPL2)");
      break;
    case ChipType::DUAL_OPL3:
      // Serial.println("Dual YMF262 (OPL3)");
      break;
    default:
      // Serial.println("Unknown");
      break;
  }
  // // Serial.print("  Total samples: ");
  // // Serial.println(header_.totalSamples);
  // // Serial.print("  Duration: ");
  // // Serial.print(header_.totalSamples / 44100.0);
  // // Serial.println(" seconds");
  if (hasLoop()) {
    // // Serial.print("  Loop: Yes at offset ");
    // // Serial.print(loopOffsetInData_);
    // // Serial.println(" (will be captured during playback)");
  }
  // // Serial.print("  Memory usage: ~");
  // // Serial.print((BUFFER_SIZE + COMPRESSED_BUFFER_SIZE + DICT_SIZE) / 1024);
  // // Serial.println(" KB (streaming)");

  return true;
}

bool VGMFile::loadVGM(const char* filename) {
  // Open file using FileSource
  file_ = fileSource_->open(filename, FILE_READ);
  if (!file_) {
    // // Serial.print("Could not open VGM file: ");
    // // Serial.println(filename);
    return false;
  }

  // Uncompressed mode
  fileMode_ = MODE_UNCOMPRESSED;

  size_t fileSize = file_.size();
  if (fileSize < sizeof(VGMHeader)) {
    // // Serial.println("VGM file too small for header");
    file_.close();
    return false;
  }

  // Read header
  if (file_.read((uint8_t*)&header_, sizeof(VGMHeader)) != sizeof(VGMHeader)) {
    // // Serial.println("Failed to read VGM header");
    file_.close();
    return false;
  }

  // Check VGM signature
  if (memcmp(header_.ident, "Vgm ", 4) != 0) {
    // // Serial.println("Invalid VGM signature");
    file_.close();
    return false;
  }

  // Parse header and detect chip type
  if (!parseHeader()) {
    file_.close();
    return false;
  }

  chipType_ = detectChipType();
  if (chipType_ == ChipType::NONE) {
    // // Serial.println("VGM file does not contain supported chip data (OPL2/OPL3/NES APU/GB/Genesis)");
    file_.close();
    return false;
  }

  // Calculate data offset
  if (header_.version < 0x150) {
    dataOffset_ = 0x40;
  } else {
    dataOffset_ = 0x34 + header_.vgmDataOffset;
  }

  if (dataOffset_ >= fileSize) {
    // // Serial.println("Invalid VGM data offset");
    file_.close();
    return false;
  }

  // Calculate data size
  vgmDataSize_ = fileSize - dataOffset_;
  fileDataStartOffset_ = dataOffset_;

  // Calculate loop offset in data (if present)
  if (hasLoop()) {
    uint32_t loopOffsetInFile = 0x1C + header_.loopOffset;
    // // Serial.print("Loop offset from header: 0x");
    // // Serial.print(header_.loopOffset, HEX);
    // // Serial.print(" (");
    // // Serial.print(header_.loopOffset);
    // // Serial.println(")");
    // // Serial.print("Loop point in file: 0x");
    // // Serial.print(loopOffsetInFile, HEX);
    // // Serial.print(" (");
    // // Serial.print(loopOffsetInFile);
    // // Serial.println(")");
    // // Serial.print("VGM data starts at: 0x");
    // // Serial.print(dataOffset_, HEX);
    // // Serial.print(" (");
    // // Serial.print(dataOffset_);
    // // Serial.println(")");

    if (loopOffsetInFile >= dataOffset_) {
      loopOffsetInData_ = loopOffsetInFile - dataOffset_;
      // // Serial.print("Loop offset in data stream: ");
      // // Serial.println(loopOffsetInData_);
    } else {
      loopOffsetInData_ = 0;
      // // Serial.println("WARNING: Loop offset is before data start!");
    }
  } else {
    loopOffsetInData_ = 0;
  }

  // Seek to data start and initialize buffer
  file_.seek(dataOffset_);
  currentDataPos_ = 0;
  bufferPos_ = 0;
  bufferSize_ = 0;

  // Pre-fill buffer
  if (!refillBuffer()) {
    // // Serial.println("Failed to read initial data");
    file_.close();
    return false;
  }

  // Print info
  // // Serial.println("VGM file loaded successfully:");
  // // Serial.print("  Version: ");
  // // Serial.println(getVersionString());
  // // Serial.print("  Chip: ");
  switch(chipType_) {
    case ChipType::YM3812_OPL2:
      // // Serial.println("YM3812 (OPL2)");
      break;
    case ChipType::YMF262_OPL3:
      // // Serial.println("YMF262 (OPL3)");
      break;
    case ChipType::DUAL_OPL2:
      // // Serial.println("Dual YM3812 (OPL2)");
      break;
    case ChipType::DUAL_OPL3:
      // Serial.println("Dual YMF262 (OPL3)");
      break;
    default:
      // Serial.println("Unknown");
      break;
  }
  // // Serial.print("  Total samples: ");
  // // Serial.println(header_.totalSamples);
  // // Serial.print("  Duration: ");
  // // Serial.print(header_.totalSamples / 44100.0);
  // // Serial.println(" seconds");
  // // Serial.print("  Data size: ");
  // // Serial.print(vgmDataSize_);
  // // Serial.println(" bytes");
  if (hasLoop()) {
    // // Serial.print("  Loop: Yes (");
    // // Serial.print(header_.loopSamples);
    // // Serial.println(" samples)");
  }

  return true;
}

bool VGMFile::parseHeader() {
  // Validate version
  uint8_t majorVersion = (header_.version >> 8) & 0xFF;
  uint8_t minorVersion = header_.version & 0xFF;

  // // Serial.print("VGM Version: ");
  // // Serial.print(majorVersion, HEX);
  // // Serial.print(".");
  // // Serial.print(minorVersion, HEX);
  // // Serial.println();

  // Check if version is reasonable (1.00 to 1.71)
  if (header_.version < 0x100 || header_.version > 0x171) {
    // // Serial.println("Warning: Unusual VGM version number");
  }

  return true;
}

ChipType VGMFile::detectChipType() {
  bool hasOPL2 = (header_.ym3812Clock != 0);
  bool hasOPL3 = (header_.ymf262Clock != 0);
  bool hasNESAPU = (header_.nesAPUClock & 0x3FFFFFFF) != 0;  // Mask dual-chip and FDS bits
  bool hasGameBoy = (header_.gbDMGClock & 0x3FFFFFFF) != 0;  // Mask dual-chip bits
  bool hasYM2612 = (header_.ym2612Clock & 0x3FFFFFFF) != 0;  // Mask dual-chip bits
  bool hasSN76489 = (header_.sn76489Clock & 0x3FFFFFFF) != 0;  // Mask dual-chip bits

  // Check for dual chip (bit 30 set in clock value)
  bool dualOPL2 = hasOPL2 && (header_.ym3812Clock & 0x40000000);
  bool dualOPL3 = hasOPL3 && (header_.ymf262Clock & 0x40000000);

  // Detect Sega Genesis configuration
  if (hasYM2612 && hasSN76489) {
    Serial.println("VGM: Detected Sega Genesis (YM2612 + SN76489)");
    Serial.print("  YM2612 clock: ");
    Serial.println(header_.ym2612Clock & 0x3FFFFFFF);
    Serial.print("  SN76489 clock: ");
    Serial.println(header_.sn76489Clock & 0x3FFFFFFF);
    return ChipType::SEGA_GENESIS;
  } else if (hasYM2612) {
    Serial.println("VGM: Detected YM2612 (FM only)");
    return ChipType::YM2612_ONLY;
  } else if (hasSN76489) {
    Serial.println("VGM: Detected SN76489 (PSG only)");
    return ChipType::SN76489_ONLY;
  }

  // OPL3 takes precedence over OPL2, then NES APU, then Game Boy
  if (dualOPL3) {
    return ChipType::DUAL_OPL3;
  } else if (hasOPL3) {
    return ChipType::YMF262_OPL3;
  } else if (dualOPL2) {
    return ChipType::DUAL_OPL2;
  } else if (hasOPL2) {
    return ChipType::YM3812_OPL2;
  } else if (hasNESAPU) {
    return ChipType::NES_APU;
  } else if (hasGameBoy) {
    return ChipType::GAMEBOY_DMG;
  }

  return ChipType::NONE;
}

bool VGMFile::refillBuffer() {
  if (fileMode_ == MODE_COMPRESSED) {
    return refillBufferCompressed();
  }

  // Uncompressed mode - read directly from file
  if (!file_ || !file_.available()) {
    bufferSize_ = 0;
    return false;
  }

  // Read next chunk
  int bytesRead = file_.read(buffer_, BUFFER_SIZE);
  if (bytesRead <= 0) {
    bufferSize_ = 0;
    return false;
  }

  bufferSize_ = bytesRead;
  bufferPos_ = 0;
  return true;
}

bool VGMFile::refillBufferCompressed() {
  if (!decompressorActive_) {
    return false;
  }

  // Reset output buffer for decompressor
  decompressor_.dest = buffer_;
  decompressor_.dest_limit = buffer_ + BUFFER_SIZE;

  // Decompress data to fill buffer
  // The callback will automatically refill compressed data as needed
  while (decompressor_.dest < decompressor_.dest_limit) {
    // Decompress next chunk
    int res = uzlib_uncompress(&decompressor_);

    if (res == TINF_DONE) {
      // End of compressed stream
      break;
    } else if (res != TINF_OK) {
      // // Serial.print("Decompression error in refill: ");
      // // Serial.println(res);
      bufferSize_ = 0;
      return false;
    }
  }

  // Calculate how much we decompressed
  bufferSize_ = decompressor_.dest - buffer_;
  bufferPos_ = 0;

  return bufferSize_ > 0;
}

bool VGMFile::readByte(uint8_t& byte) {
  // Check if we need to refill buffer
  if (bufferPos_ >= bufferSize_) {
    if (!refillBuffer()) {
      return false;
    }
  }

  // Check bounds
  if (currentDataPos_ >= vgmDataSize_) {
    return false;
  }

  // Capture loop snapshot BEFORE reading the byte at the loop point (VGZ streaming only)
  // This captures the state right before we read the loop point byte
  if (fileMode_ == MODE_COMPRESSED &&
      hasLoop() &&
      !loopSnapshot_.valid &&
      currentDataPos_ == loopOffsetInData_) {
    captureLoopSnapshot();
  }

  // Read byte
  byte = buffer_[bufferPos_++];
  currentDataPos_++;

  return true;
}

bool VGMFile::peekByte(uint8_t& byte) {
  // Check if we need to refill buffer
  if (bufferPos_ >= bufferSize_) {
    if (!refillBuffer()) {
      return false;
    }
  }

  // Check bounds
  if (currentDataPos_ >= vgmDataSize_) {
    return false;
  }

  // Peek byte without advancing
  byte = buffer_[bufferPos_];
  return true;
}

bool VGMFile::seekToDataPosition(uint32_t position) {
  if (position >= vgmDataSize_) {
    return false;
  }

  // For compressed files, we can only seek to the loop point via snapshot
  if (fileMode_ == MODE_COMPRESSED) {
    if (hasLoop() && position == loopOffsetInData_) {
      // Try to restore loop snapshot
      if (loopSnapshot_.valid) {
        return restoreLoopSnapshot();
      } else {
        // // Serial.println("Loop snapshot not yet available (need to reach loop point first)");
        return false;
      }
    } else {
      // // Serial.println("Cannot seek in compressed VGZ file (only loop points supported)");
      return false;
    }
  }

  // Uncompressed mode - seek normally
  // Calculate absolute file position
  uint32_t filePos = fileDataStartOffset_ + position;

  // Seek in file
  if (!file_.seek(filePos)) {
    return false;
  }

  // Reset buffer
  currentDataPos_ = position;
  bufferPos_ = 0;
  bufferSize_ = 0;

  // Refill buffer
  return refillBuffer();
}

String VGMFile::getVersionString() const {
  uint8_t major = (header_.version >> 8) & 0xFF;
  uint8_t minor = header_.version & 0xFF;

  String result = String(major >> 4) + "." + String(major & 0x0F);
  result += String(minor >> 4) + String(minor & 0x0F);
  return result;
}

uint32_t VGMFile::readLE32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

uint16_t VGMFile::readLE16(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}

void VGMFile::captureLoopSnapshot() {
  if (fileMode_ != MODE_COMPRESSED || !decompressorActive_) {
    return;
  }

  // // Serial.print("Capturing loop snapshot at decompressed data position ");
  // // Serial.println(currentDataPos_);

  // Calculate where we are in the compressed stream
  // The decompressor's source pointer tells us the offset into compressedBuffer_
  size_t offsetIntoBuffer = decompressor_.source - compressedBuffer_;

  // file_.position() tells us where we'd read the NEXT chunk from
  // We need to calculate where the CURRENT buffer started
  uint32_t currentFilePos = file_.position();
  size_t bytesInBuffer = decompressor_.source_limit - compressedBuffer_;
  uint32_t bufferStartPos = currentFilePos - bytesInBuffer;

  // The exact compressed file position is where the buffer started + offset
  loopSnapshot_.compressedFilePos = bufferStartPos + offsetIntoBuffer;
  loopSnapshot_.compressedBufferOffset = offsetIntoBuffer;

  // // Serial.print("  File position: ");
  // // Serial.print(currentFilePos);
  // // Serial.print(", buffer start: ");
  // // Serial.print(bufferStartPos);
  // // Serial.print(", offset in buffer: ");
  // // Serial.print(offsetIntoBuffer);
  // // Serial.print(", snapshot at: ");
  // // Serial.println(loopSnapshot_.compressedFilePos);

  // Check where the decompressor's dest pointer is
  size_t destOffset = decompressor_.dest - buffer_;
  // // Serial.print("  Decompressor dest offset in output buffer: ");
  // // Serial.println(destOffset);
  // // Serial.print("  Current bufferPos_: ");
  // // Serial.println(bufferPos_);

  loopSnapshot_.decompressedDataPos = currentDataPos_;

  // Copy decompressor state
  memcpy(&loopSnapshot_.decompressorState, &decompressor_, sizeof(decompressor_));

  // Copy dictionary if it exists
  if (decompressor_.dict_ring && decompressor_.dict_size > 0) {
    if (!loopSnapshot_.dictCopy) {
      loopSnapshot_.dictCopy = new uint8_t[decompressor_.dict_size];
    }
    if (loopSnapshot_.dictCopy) {
      memcpy(loopSnapshot_.dictCopy, decompressor_.dict_ring, decompressor_.dict_size);
      loopSnapshot_.dictSize = decompressor_.dict_size;
    } else {
      // // Serial.println("WARNING: Failed to allocate loop snapshot dictionary!");
      return;
    }
  }

  // Save the decompressed buffer data from current position to end
  // This is the data we'll use when we loop back
  size_t bytesRemaining = bufferSize_ - bufferPos_;
  if (bytesRemaining > 0) {
    loopSnapshot_.savedBufferData = new uint8_t[bytesRemaining];
    if (loopSnapshot_.savedBufferData) {
      memcpy(loopSnapshot_.savedBufferData, buffer_ + bufferPos_, bytesRemaining);
      loopSnapshot_.savedBufferSize = bytesRemaining;
      // // Serial.print("  Saved ");
      // // Serial.print(bytesRemaining);
      // // Serial.println(" bytes of decompressed buffer data");
    } else {
      // // Serial.println("WARNING: Failed to allocate saved buffer data!");
      return;
    }
  } else {
    loopSnapshot_.savedBufferData = nullptr;
    loopSnapshot_.savedBufferSize = 0;
    // // Serial.println("  No buffered data to save");
  }

  loopSnapshot_.valid = true;
  // // Serial.println("Loop snapshot captured successfully!");
}

bool VGMFile::restoreLoopSnapshot() {
  if (!loopSnapshot_.valid) {
    // // Serial.println("Cannot restore loop: snapshot not valid");
    return false;
  }

  // // Serial.println("Restoring loop snapshot...");
  // // Serial.print("  Seeking to EXACT compressed position: ");
  // // Serial.println(loopSnapshot_.compressedFilePos);

  // Seek to the EXACT position in the compressed file
  if (!file_.seek(loopSnapshot_.compressedFilePos)) {
    // // Serial.println("Failed to seek to loop position in compressed file");
    return false;
  }

  // Read fresh compressed data starting from this position
  int bytesRead = file_.read(compressedBuffer_, COMPRESSED_BUFFER_SIZE);
  if (bytesRead <= 0) {
    // // Serial.println("Failed to read compressed data at loop position");
    return false;
  }

  // // Serial.print("  Read ");
  // // Serial.print(bytesRead);
  // // Serial.println(" bytes of compressed data");

  // Save current buffer pointers before restoring state
  uint8_t* savedDictPtr = decompressor_.dict_ring;

  // Restore decompressor state
  memcpy(&decompressor_, &loopSnapshot_.decompressorState, sizeof(decompressor_));

  // Fix up ALL pointers to point to our current buffers
  decompressor_.dict_ring = savedDictPtr;  // Restore our dict buffer pointer
  decompressor_.dest_start = buffer_;
  decompressor_.dest = buffer_;
  decompressor_.dest_limit = buffer_ + BUFFER_SIZE;

  // IMPORTANT: Source starts at beginning of freshly read buffer
  decompressor_.source = compressedBuffer_;
  decompressor_.source_limit = compressedBuffer_ + bytesRead;

  // Restore dictionary contents
  if (loopSnapshot_.dictCopy && loopSnapshot_.dictSize > 0 && decompressor_.dict_ring) {
    memcpy(decompressor_.dict_ring, loopSnapshot_.dictCopy, loopSnapshot_.dictSize);
    decompressor_.dict_size = loopSnapshot_.dictSize;
  }

  // Set current position
  currentDataPos_ = loopSnapshot_.decompressedDataPos;

  // Use the saved decompressed buffer data directly!
  // This avoids the problem of decompressing from a different state
  if (loopSnapshot_.savedBufferData && loopSnapshot_.savedBufferSize > 0) {
    // Copy saved data to buffer
    memcpy(buffer_, loopSnapshot_.savedBufferData, loopSnapshot_.savedBufferSize);
    bufferSize_ = loopSnapshot_.savedBufferSize;
    bufferPos_ = 0;

    // // Serial.print("Restored ");
    // // Serial.print(bufferSize_);
    // // Serial.println(" bytes of saved decompressed data");
  } else {
    // No saved data - need to decompress fresh
    // // Serial.println("No saved buffer data, decompressing fresh...");
    bufferPos_ = 0;
    bufferSize_ = 0;
    decompressor_.dest = buffer_;

    // Decompress to fill buffer from this point
    while (decompressor_.dest < decompressor_.dest_limit) {
      int res = uzlib_uncompress(&decompressor_);
      if (res == TINF_DONE || res != TINF_OK) {
        break;
      }
    }

    bufferSize_ = decompressor_.dest - buffer_;
    if (bufferSize_ == 0) {
      // // Serial.println("Failed to decompress after loop restore");
      return false;
    }
  }

  // // Serial.print("Loop restored to data position ");
  // // Serial.println(currentDataPos_);
  // // Serial.print("Buffer has ");
  // // Serial.print(bufferSize_);
  // // Serial.println(" bytes ready");
  return true;
}

// ========== PCM Data Bank Implementation (PSRAM-based) ==========

bool VGMFile::allocateDataBank() {
  if (dataBank_) {
    // Already allocated
    return true;
  }

  // Allocate in PSRAM using direct extmem_malloc
  dataBank_ = (uint8_t*)extmem_malloc(MAX_DATA_BANK_SIZE);
  if (!dataBank_) {
    Serial.println("VGM: Failed to allocate data bank in PSRAM");
    return false;
  }

  dataBankSize_ = 0;
  dataBankPos_ = 0;

  Serial.print("VGM: Allocated ");
  Serial.print(MAX_DATA_BANK_SIZE / 1024);
  Serial.println("KB data bank in PSRAM");

  return true;
}

void VGMFile::clearDataBank() {
  if (dataBank_) {
    extmem_free(dataBank_);
    dataBank_ = nullptr;
  }
  dataBankSize_ = 0;
  dataBankPos_ = 0;
}

void VGMFile::appendToDataBank(const uint8_t* data, uint32_t size) {
  if (!dataBank_ && !allocateDataBank()) {
    return;  // Can't allocate
  }

  // Check if we have space
  if (dataBankSize_ + size > MAX_DATA_BANK_SIZE) {
    Serial.print("VGM: Data bank overflow! Requested ");
    Serial.print(size);
    Serial.print(" bytes, have ");
    Serial.print(MAX_DATA_BANK_SIZE - dataBankSize_);
    Serial.println(" bytes available");
    // Truncate to fit
    size = MAX_DATA_BANK_SIZE - dataBankSize_;
  }

  if (size > 0) {
    memcpy(dataBank_ + dataBankSize_, data, size);
    dataBankSize_ += size;
  }
}

bool VGMFile::readDataBankByte(uint8_t& byte) {
  if (!dataBank_ || dataBankPos_ >= dataBankSize_) {
    // No data bank or at end - return silence
    byte = 0x80;  // Silence for YM2612 DAC (unsigned 8-bit center)
    return false;
  }

  byte = dataBank_[dataBankPos_++];
  return true;
}

void VGMFile::seekDataBank(uint32_t offset) {
  if (offset < dataBankSize_) {
    dataBankPos_ = offset;
  } else {
    // Clamp to end
    dataBankPos_ = dataBankSize_;
  }
}

// ========== Stream Control Implementation ==========

void VGMFile::setupStream(uint8_t streamID, uint8_t chipType, uint8_t port, uint8_t command) {
  if (streamID >= MAX_STREAMS) return;

  StreamState& stream = streams_[streamID];
  stream.chipType = chipType;
  stream.port = port;
  stream.command = command;
  stream.active = false;  // Not active until started
}

void VGMFile::setStreamData(uint8_t streamID, uint8_t dataBankID, uint8_t stepSize, uint8_t stepBase) {
  if (streamID >= MAX_STREAMS) return;

  StreamState& stream = streams_[streamID];
  stream.dataBankID = dataBankID;
  stream.stepSize = stepSize;
  // stepBase is ignored (compatibility parameter)
}

void VGMFile::setStreamFrequency(uint8_t streamID, uint32_t frequency) {
  if (streamID >= MAX_STREAMS) return;

  StreamState& stream = streams_[streamID];
  stream.frequency = frequency;
}

void VGMFile::startStream(uint8_t streamID, uint32_t dataStart, uint8_t lengthMode, uint32_t dataLength) {
  if (streamID >= MAX_STREAMS) return;

  StreamState& stream = streams_[streamID];
  stream.dataStart = dataStart;
  stream.dataPos = 0;  // Reset to beginning

  // Length mode: 0 = bytes, 1 = samples (samples need to account for stepSize)
  if (lengthMode == 1) {
    stream.dataLength = dataLength * (stream.stepSize + 1);
  } else {
    stream.dataLength = dataLength;
  }

  stream.loop = false;  // VGM spec: streams don't loop by default
  stream.active = true;
  stream.nextUpdateTime = micros();  // Start immediately
}

void VGMFile::stopStream(uint8_t streamID) {
  if (streamID >= MAX_STREAMS) return;

  streams_[streamID].active = false;
}

void VGMFile::startStreamFast(uint8_t streamID, uint16_t blockID, uint8_t flags) {
  // Fast call format (command 0x95):
  // This is a shortcut that references pre-configured stream settings
  // For now, we implement a simplified version
  // Real implementation would need a table of pre-configured blocks

  if (streamID >= MAX_STREAMS) return;

  StreamState& stream = streams_[streamID];

  // Flags control loop behavior
  stream.loop = (flags & 0x01) != 0;

  // BlockID would reference a pre-configured setup
  // For simplicity, just activate the stream with current settings
  stream.active = true;
  stream.dataPos = 0;
  stream.nextUpdateTime = micros();
}

void VGMFile::resetStreamPositions() {
  // Reset all active stream positions to beginning
  // This is called when looping back to ensure streams restart correctly
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (streams_[i].active) {
      streams_[i].dataPos = 0;
      streams_[i].nextUpdateTime = micros();
      Serial.print("[VGM Stream] Reset stream ");
      Serial.print(i);
      Serial.println(" position to 0");
    }
  }
}

void VGMFile::updateStreams(GenesisBoard* genesisBoard) {
  // Hardware DAC mode only - pre-rendered DAC handles streams internally
  if (!genesisBoard || !dataBank_) return;

  uint32_t now = micros();

  for (int i = 0; i < MAX_STREAMS; i++) {
    StreamState& stream = streams_[i];

    if (!stream.active || stream.frequency == 0) continue;

    // Calculate interval in microseconds
    uint32_t intervalUs = 1000000UL / stream.frequency;

    // Emit as many samples as needed to catch up
    while ((int32_t)(now - stream.nextUpdateTime) >= 0) {
      stream.nextUpdateTime += intervalUs;

      // Read next sample from data bank
      uint32_t absolutePos = stream.dataStart + stream.dataPos;

      if (absolutePos >= dataBankSize_) {
        // Out of data
        stream.active = false;
        break;
      }

      uint8_t sample = dataBank_[absolutePos];

      // Write to chip (assumes YM2612 DAC for now)
      if (stream.chipType == 0x02) {  // YM2612
        // For DAC streaming, use fast direct write (not the slow register path)
        if (stream.command == 0x2A) {  // DAC data register
          // Hardware DAC - writeDAC handles streaming mode internally
          genesisBoard->writeDAC(sample);
        } else {
          // Non-DAC register write
          genesisBoard->writeYM2612(stream.port, stream.command, sample);
        }
      }

      // Advance position
      stream.dataPos += (stream.stepSize + 1);

      // Check if we've reached the end
      if (stream.dataPos >= stream.dataLength) {
        if (stream.loop) {
          stream.dataPos = 0;  // Loop back to start
        } else {
          stream.active = false;  // Stop
          break;
        }
      }
    }
  }
}
