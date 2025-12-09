/**
 * @file dac_prerender.cpp
 * @brief Implementation of YM2612 DAC pre-renderer
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#include "dac_prerender.h"
#include "vgm_file.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

DACPrerenderer::DACPrerenderer()
    : dacValue_(0x80)           // Center/silence
    , dacEnabled_(false)
    , panning_(0xC0)            // Center (both speakers)
    , currentSample_(0)
    , dataBank_(nullptr)
    , dataBankSize_(0)
    , dataBankCapacity_(0)
    , dataBankPos_(0)
    , dataBankInPSRAM_(false)
    , totalSamplesRendered_(0)
    , loopPointSample_(NO_LOOP)
    , error_(nullptr)
    , progressCallback_(nullptr)
    , progressUserData_(nullptr)
    , lastProgressUpdate_(0)
    , writeBufferPos_(0) {

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
        streams_[i].accumulator = 0.0f;
        streams_[i].samplesPerTick = 1.0f;
    }
}

DACPrerenderer::~DACPrerenderer() {
    // Close file if still open
    if (outputFile_) {
        outputFile_.close();
    }
    // Free our data bank using the correct allocator
    if (dataBank_) {
        if (dataBankInPSRAM_) {
            extmem_free(dataBank_);
        } else {
            free(dataBank_);
        }
        dataBank_ = nullptr;
        dataBankInPSRAM_ = false;
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void DACPrerenderer::setProgressCallback(ProgressCallback callback, void* userData) {
    progressCallback_ = callback;
    progressUserData_ = userData;
}

bool DACPrerenderer::preRender(VGMFile* vgmFile, const char* outputPath) {
    if (!vgmFile) {
        error_ = "VGM file is null";
        return false;
    }

    if (!outputPath || outputPath[0] == '\0') {
        error_ = "Output path is empty";
        return false;
    }

    Serial.println("[DACPrerender] Starting pre-render...");
    uint32_t startTime = millis();

    // Reset state (this also frees any previous data bank)
    resetState();

    // Pre-allocate data bank in PSRAM for PCM samples
    // Data will be loaded as we encounter 0x67 commands
    dataBank_ = (uint8_t*)extmem_malloc(MAX_DATA_BANK_SIZE);
    if (dataBank_) {
        dataBankInPSRAM_ = true;
    } else {
        // Fall back to regular malloc if PSRAM not available
        dataBank_ = (uint8_t*)malloc(MAX_DATA_BANK_SIZE);
        dataBankInPSRAM_ = false;
    }
    if (!dataBank_) {
        error_ = "Failed to allocate data bank";
        Serial.println("[DACPrerender] ERROR: Failed to allocate data bank");
        return false;
    }
    dataBankCapacity_ = MAX_DATA_BANK_SIZE;
    dataBankSize_ = 0;
    dataBankPos_ = 0;

    Serial.printf("[DACPrerender] Data bank allocated: %lu bytes at 0x%08X\n",
                  dataBankCapacity_, (uint32_t)dataBank_);

    // Get total samples and loop point from VGM file
    uint32_t totalSamples = vgmFile->getTotalSamples();
    loopPointSample_ = vgmFile->hasLoop() ? vgmFile->getLoopPointSample() : NO_LOOP;

    Serial.printf("[DACPrerender] Total samples: %lu (%.2f seconds)\n",
                  totalSamples, totalSamples / 44100.0f);
    if (loopPointSample_ != NO_LOOP) {
        Serial.printf("[DACPrerender] Loop point at sample %lu (%.2f seconds)\n",
                      loopPointSample_, loopPointSample_ / 44100.0f);
    }

    // Delete existing file if present
    if (SD.exists(outputPath)) {
        SD.remove(outputPath);
    }

    // Open output file
    outputFile_ = SD.open(outputPath, FILE_WRITE);
    if (!outputFile_) {
        error_ = "Failed to create output file";
        Serial.printf("[DACPrerender] ERROR: %s: %s\n", error_, outputPath);
        return false;
    }

    // Write initial header (will be updated at the end)
    if (!writeHeader(totalSamples, loopPointSample_)) {
        outputFile_.close();
        SD.remove(outputPath);
        return false;
    }

    // NOTE: We do NOT seek the VGM file here!
    // For VGZ files, seeking doesn't work (compressed stream).
    // The VGM file should already be at the start of data after loadFromFile().
    // We read through it sequentially during pre-render.

    // Process all VGM commands
    uint8_t cmd;
    uint32_t commandsProcessed = 0;

    while (!vgmFile->isAtEnd()) {
        if (!vgmFile->readByte(cmd)) {
            break;  // End of data
        }

        if (!processCommand(vgmFile, cmd)) {
            break;  // End of data (0x66) or error
        }

        commandsProcessed++;

        // Report progress periodically
        if (commandsProcessed % 10000 == 0) {
            reportProgress(currentSample_, totalSamples);
        }
    }

    // Flush any remaining data in write buffer
    if (!flushWriteBuffer()) {
        outputFile_.close();
        SD.remove(outputPath);
        error_ = "Failed to flush write buffer";
        return false;
    }

    // Update header with actual sample count
    totalSamplesRendered_ = currentSample_;
    if (!updateHeader()) {
        outputFile_.close();
        SD.remove(outputPath);
        return false;
    }

    // Close output file
    outputFile_.close();

    uint32_t elapsed = millis() - startTime;
    uint32_t fileSize = totalSamplesRendered_ * 2 + HEADER_SIZE;

    Serial.printf("[DACPrerender] Complete! %lu samples (%.2f sec) in %lu ms\n",
                  totalSamplesRendered_, totalSamplesRendered_ / 44100.0f, elapsed);
    Serial.printf("[DACPrerender] File size: %lu bytes (%.2f MB)\n",
                  fileSize, fileSize / (1024.0f * 1024.0f));
    Serial.printf("[DACPrerender] Speed: %.1fx realtime\n",
                  (totalSamplesRendered_ / 44100.0f) / (elapsed / 1000.0f));

    return true;
}

// ============================================================================
// Private Methods - State Management
// ============================================================================

void DACPrerenderer::resetState() {
    dacValue_ = 0x80;           // Center/silence
    dacEnabled_ = false;
    panning_ = 0xC0;            // Center (both speakers)
    currentSample_ = 0;

    // Free existing data bank using the correct allocator
    if (dataBank_) {
        if (dataBankInPSRAM_) {
            extmem_free(dataBank_);
        } else {
            free(dataBank_);
        }
        dataBank_ = nullptr;
        dataBankInPSRAM_ = false;
    }
    dataBankSize_ = 0;
    dataBankCapacity_ = 0;
    dataBankPos_ = 0;

    totalSamplesRendered_ = 0;
    loopPointSample_ = NO_LOOP;
    error_ = nullptr;
    lastProgressUpdate_ = 0;
    writeBufferPos_ = 0;

    // Reset streams
    for (int i = 0; i < MAX_STREAMS; i++) {
        streams_[i].active = false;
        streams_[i].accumulator = 0.0f;
    }
}

// ============================================================================
// Private Methods - File I/O
// ============================================================================

bool DACPrerenderer::writeHeader(uint32_t totalSamples, uint32_t loopPoint) {
    uint8_t header[HEADER_SIZE];

    // Magic "DAC1"
    header[0] = 'D';
    header[1] = 'A';
    header[2] = 'C';
    header[3] = '1';

    // Total samples (little-endian)
    header[4] = totalSamples & 0xFF;
    header[5] = (totalSamples >> 8) & 0xFF;
    header[6] = (totalSamples >> 16) & 0xFF;
    header[7] = (totalSamples >> 24) & 0xFF;

    // Loop point (little-endian)
    header[8] = loopPoint & 0xFF;
    header[9] = (loopPoint >> 8) & 0xFF;
    header[10] = (loopPoint >> 16) & 0xFF;
    header[11] = (loopPoint >> 24) & 0xFF;

    // Reserved (flags)
    header[12] = 0;
    header[13] = 0;
    header[14] = 0;
    header[15] = 0;

    size_t written = outputFile_.write(header, HEADER_SIZE);
    if (written != HEADER_SIZE) {
        error_ = "Failed to write header";
        return false;
    }

    return true;
}

bool DACPrerenderer::updateHeader() {
    // Seek back to header
    if (!outputFile_.seek(0)) {
        error_ = "Failed to seek to header";
        return false;
    }

    // Rewrite header with actual values
    return writeHeader(totalSamplesRendered_, loopPointSample_);
}

bool DACPrerenderer::flushWriteBuffer() {
    if (writeBufferPos_ == 0) {
        return true;  // Nothing to flush
    }

    size_t written = outputFile_.write(writeBuffer_, writeBufferPos_);
    if (written != writeBufferPos_) {
        error_ = "Failed to write to output file";
        return false;
    }

    writeBufferPos_ = 0;
    return true;
}

void DACPrerenderer::writeSample(uint8_t dacValue, uint8_t panning, bool dacEnabled) {
    // Build sample data (2 bytes)
    uint8_t flags = panningToFlags(panning);
    if (dacEnabled) {
        flags |= FLAG_DAC_ENABLED;
    }

    // Add to write buffer
    writeBuffer_[writeBufferPos_++] = dacValue;
    writeBuffer_[writeBufferPos_++] = flags;

    // Flush buffer if full
    if (writeBufferPos_ >= WRITE_BUFFER_SIZE) {
        flushWriteBuffer();
    }

    currentSample_++;
}

void DACPrerenderer::writeSamples(uint32_t count) {
    // Write multiple identical samples (current state)
    for (uint32_t i = 0; i < count; i++) {
        writeSample(dacValue_, panning_, dacEnabled_);
    }
}

uint8_t DACPrerenderer::panningToFlags(uint8_t panReg) const {
    // Convert YM2612 register 0xB6 to our flag format
    // Bit 7 = Left enable, Bit 6 = Right enable
    bool left = (panReg & 0x80) != 0;
    bool right = (panReg & 0x40) != 0;

    if (left && right) {
        return FLAG_PAN_CENTER;
    } else if (left) {
        return FLAG_PAN_LEFT;
    } else if (right) {
        return FLAG_PAN_RIGHT;
    } else {
        return FLAG_PAN_MUTE;
    }
}

void DACPrerenderer::reportProgress(uint32_t current, uint32_t total) {
    if (!progressCallback_) return;

    uint32_t now = millis();
    if (now - lastProgressUpdate_ < 100) return;  // Rate limit to 10Hz
    lastProgressUpdate_ = now;

    float progress = (total > 0) ? (float)current / (float)total : 0.0f;
    progressCallback_(progress, progressUserData_);
}

// ============================================================================
// Private Methods - Command Processing
// ============================================================================

bool DACPrerenderer::processCommand(VGMFile* vgmFile, uint8_t cmd) {
    uint8_t reg, val, byte1, byte2, byte3, byte4;

    switch (cmd) {
        // ========== YM2612 Port 0 Write (0x52) ==========
        case 0x52:
            if (!vgmFile->readByte(reg) || !vgmFile->readByte(val)) {
                return false;
            }
            if (reg == 0x2A) {
                // DAC data write
                dacValue_ = val;
            } else if (reg == 0x2B) {
                // DAC enable (bit 7)
                dacEnabled_ = (val & 0x80) != 0;
            }
            // Other port 0 registers are FM - ignore
            break;

        // ========== YM2612 Port 1 Write (0x53) ==========
        case 0x53:
            if (!vgmFile->readByte(reg) || !vgmFile->readByte(val)) {
                return false;
            }
            if (reg == 0xB6) {
                // Channel 6 output control (panning)
                // Only relevant when DAC is using channel 6
                panning_ = val;
            }
            // Other port 1 registers are FM - ignore
            break;

        // ========== Wait Commands ==========
        case 0x61: {
            // Wait n samples (16-bit)
            if (!vgmFile->readByte(byte1) || !vgmFile->readByte(byte2)) {
                return false;
            }
            uint16_t wait = byte1 | (byte2 << 8);
            updateStreamsToSample(currentSample_ + wait);
            break;
        }

        case 0x62:
            // Wait 735 samples (1/60 second, NTSC frame)
            updateStreamsToSample(currentSample_ + 735);
            break;

        case 0x63:
            // Wait 882 samples (1/50 second, PAL frame)
            updateStreamsToSample(currentSample_ + 882);
            break;

        // ========== Data Bank Commands ==========
        case 0xE0: {
            // Seek to offset in PCM data bank
            if (!vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
                !vgmFile->readByte(byte3) || !vgmFile->readByte(byte4)) {
                return false;
            }
            dataBankPos_ = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);
            break;
        }

        case 0x67: {
            // Data block - read and store PCM data
            // Format: 0x67 0x66 tt ss ss ss ss [data]
            uint8_t check;
            if (!vgmFile->readByte(check) || check != 0x66) {
                error_ = "Invalid data block format";
                return false;
            }

            uint8_t dataType;
            if (!vgmFile->readByte(dataType)) {
                return false;
            }

            if (!vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
                !vgmFile->readByte(byte3) || !vgmFile->readByte(byte4)) {
                return false;
            }
            uint32_t dataSize = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

            if (dataType == 0x00) {
                // YM2612 PCM data - read and store in our data bank
                Serial.printf("[DACPrerender] Loading %lu bytes of PCM data (bank pos %lu)\n",
                              dataSize, dataBankSize_);

                // Check if we have space
                if (dataBankSize_ + dataSize > dataBankCapacity_) {
                    Serial.printf("[DACPrerender] WARNING: Data bank overflow! Have %lu, need %lu\n",
                                  dataBankCapacity_ - dataBankSize_, dataSize);
                    dataSize = dataBankCapacity_ - dataBankSize_;  // Truncate
                }

                // Read data directly into our data bank
                for (uint32_t i = 0; i < dataSize; i++) {
                    if (!vgmFile->readByte(dataBank_[dataBankSize_])) {
                        return false;
                    }
                    dataBankSize_++;
                }

                Serial.printf("[DACPrerender] Data bank now %lu bytes\n", dataBankSize_);
            } else {
                // Unknown data type - skip it
                for (uint32_t i = 0; i < dataSize; i++) {
                    if (!vgmFile->readByte(byte1)) {
                        return false;
                    }
                }
            }
            break;
        }

        // ========== Stream Control Commands ==========
        case 0x90:
            processStreamSetup(vgmFile);
            break;

        case 0x91:
            processStreamData(vgmFile);
            break;

        case 0x92:
            processStreamFrequency(vgmFile);
            break;

        case 0x93:
            processStreamStart(vgmFile);
            break;

        case 0x94:
            processStreamStop(vgmFile);
            break;

        case 0x95:
            processStreamFast(vgmFile);
            break;

        // ========== End of Data ==========
        case 0x66:
            // End of sound data
            return false;

        // ========== Other Commands ==========
        default:
            if ((cmd & 0xF0) == 0x80) {
                // 0x8n: Read from data bank, write to DAC, wait n samples
                // Use OUR data bank, not VGMFile's
                uint8_t sample = 0x80;  // Default to silence
                if (dataBank_ && dataBankPos_ < dataBankSize_) {
                    sample = dataBank_[dataBankPos_++];
                }
                dacValue_ = sample;

                uint8_t wait = cmd & 0x0F;
                updateStreamsToSample(currentSample_ + wait);
            } else if ((cmd & 0xF0) == 0x70) {
                // 0x7n: Wait n+1 samples
                uint8_t wait = (cmd & 0x0F) + 1;
                updateStreamsToSample(currentSample_ + wait);
            } else {
                // Skip unhandled command
                skipCommand(vgmFile, cmd);
            }
            break;
    }

    return true;
}

void DACPrerenderer::skipCommand(VGMFile* vgmFile, uint8_t cmd) {
    // Skip bytes for commands we don't handle
    // Based on VGM specification command lengths

    uint8_t dummy;
    int skipBytes = 0;

    if (cmd >= 0x30 && cmd <= 0x3F) {
        skipBytes = 1;  // Reserved single-byte commands
    } else if (cmd >= 0x40 && cmd <= 0x4E) {
        skipBytes = 2;  // Various chip writes
    } else if (cmd == 0x4F) {
        skipBytes = 1;  // Game Gear PSG
    } else if (cmd == 0x50) {
        skipBytes = 1;  // PSG write
    } else if (cmd >= 0x51 && cmd <= 0x5F) {
        skipBytes = 2;  // Various chip writes (except 0x52, 0x53 handled above)
    } else if (cmd >= 0xA0 && cmd <= 0xBF) {
        skipBytes = 2;  // Secondary chip writes
    } else if (cmd >= 0xC0 && cmd <= 0xDF) {
        skipBytes = 3;  // Three-byte commands
    } else if (cmd >= 0xE1 && cmd <= 0xFF) {
        skipBytes = 4;  // Four-byte commands (except 0xE0 handled above)
    }

    for (int i = 0; i < skipBytes; i++) {
        vgmFile->readByte(dummy);
    }
}

// ============================================================================
// Private Methods - Stream Control
// ============================================================================

void DACPrerenderer::processStreamSetup(VGMFile* vgmFile) {
    // 0x90: Setup stream control
    // Format: 0x90 ss tt pp cc
    uint8_t streamID, chipType, port, command;

    if (!vgmFile->readByte(streamID) || !vgmFile->readByte(chipType) ||
        !vgmFile->readByte(port) || !vgmFile->readByte(command)) {
        return;
    }

    // Note: there's a reserved byte between chipType and port in some specs
    // The VGMFile::setupStream already handles this, but we're reading directly

    if (streamID >= MAX_STREAMS) return;

    StreamState& stream = streams_[streamID];
    stream.chipType = chipType & 0x7F;  // Mask off dual-chip bit
    stream.port = port;
    stream.command = command;
    stream.active = false;  // Not active until started
}

void DACPrerenderer::processStreamData(VGMFile* vgmFile) {
    // 0x91: Set stream data parameters
    // Format: 0x91 ss dd ll bb
    uint8_t streamID, dataBankID, stepSize, stepBase;

    if (!vgmFile->readByte(streamID) || !vgmFile->readByte(dataBankID) ||
        !vgmFile->readByte(stepSize) || !vgmFile->readByte(stepBase)) {
        return;
    }

    if (streamID >= MAX_STREAMS) return;

    StreamState& stream = streams_[streamID];
    stream.dataBankID = dataBankID;
    stream.stepSize = stepSize;
    // stepBase is ignored (compatibility parameter)
}

void DACPrerenderer::processStreamFrequency(VGMFile* vgmFile) {
    // 0x92: Set stream frequency
    // Format: 0x92 ss ff ff ff ff
    uint8_t streamID, byte1, byte2, byte3, byte4;

    if (!vgmFile->readByte(streamID) ||
        !vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
        !vgmFile->readByte(byte3) || !vgmFile->readByte(byte4)) {
        return;
    }

    if (streamID >= MAX_STREAMS) return;

    uint32_t frequency = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

    StreamState& stream = streams_[streamID];
    stream.frequency = frequency;

    // Calculate samples per tick for resampling
    // At 8 kHz stream, each stream sample spans 5.5125 output samples (44100/8000)
    if (frequency > 0) {
        stream.samplesPerTick = 44100.0f / (float)frequency;
    } else {
        stream.samplesPerTick = 1.0f;
    }
}

void DACPrerenderer::processStreamStart(VGMFile* vgmFile) {
    // 0x93: Start stream
    // Format: 0x93 ss aa aa aa aa mm ll ll ll ll
    uint8_t streamID, lengthMode;
    uint8_t byte1, byte2, byte3, byte4;

    if (!vgmFile->readByte(streamID) ||
        !vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
        !vgmFile->readByte(byte3) || !vgmFile->readByte(byte4)) {
        return;
    }

    uint32_t dataStart = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

    if (!vgmFile->readByte(lengthMode) ||
        !vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
        !vgmFile->readByte(byte3) || !vgmFile->readByte(byte4)) {
        return;
    }

    uint32_t dataLength = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

    if (streamID >= MAX_STREAMS) return;

    StreamState& stream = streams_[streamID];

    // Handle special value -1 (0xFFFFFFFF) for dataStart
    if (dataStart != 0xFFFFFFFF) {
        stream.dataStart = dataStart;
        stream.dataPos = 0;
    }
    // If dataStart is -1, keep current position

    // Length mode handling
    // Bit 7 = loop, Bit 4 = reverse (not implemented), Bits 0-3 = mode
    stream.loop = (lengthMode & 0x80) != 0;
    uint8_t mode = lengthMode & 0x0F;

    switch (mode) {
        case 0x00:
            // Ignore length (just change position)
            break;
        case 0x01:
            // Length = number of commands
            stream.dataLength = dataLength * (stream.stepSize + 1);
            break;
        case 0x02:
            // Length in milliseconds (convert to bytes)
            // dataLength ms at stream.frequency Hz
            stream.dataLength = (dataLength * stream.frequency / 1000) * (stream.stepSize + 1);
            break;
        case 0x03:
            // Play until end of data bank
            stream.dataLength = dataBankSize_ - stream.dataStart;
            break;
        default:
            stream.dataLength = dataLength;
            break;
    }

    stream.active = true;
    stream.accumulator = 0.0f;  // Reset resampling accumulator
}

void DACPrerenderer::processStreamStop(VGMFile* vgmFile) {
    // 0x94: Stop stream
    // Format: 0x94 ss
    uint8_t streamID;

    if (!vgmFile->readByte(streamID)) {
        return;
    }

    if (streamID == 0xFF) {
        // Stop all streams
        for (int i = 0; i < MAX_STREAMS; i++) {
            streams_[i].active = false;
        }
    } else if (streamID < MAX_STREAMS) {
        streams_[streamID].active = false;
    }
}

void DACPrerenderer::processStreamFast(VGMFile* vgmFile) {
    // 0x95: Start stream (fast call)
    // Format: 0x95 ss bb bb ff
    uint8_t streamID, byte1, byte2, flags;

    if (!vgmFile->readByte(streamID) ||
        !vgmFile->readByte(byte1) || !vgmFile->readByte(byte2) ||
        !vgmFile->readByte(flags)) {
        return;
    }

    if (streamID >= MAX_STREAMS) return;

    uint16_t blockID = byte1 | (byte2 << 8);
    (void)blockID;  // Block ID references pre-configured blocks (not fully implemented)

    StreamState& stream = streams_[streamID];
    stream.loop = (flags & 0x01) != 0;
    stream.active = true;
    stream.dataPos = 0;
    stream.accumulator = 0.0f;
}

void DACPrerenderer::updateStreamsToSample(uint32_t targetSample) {
    // This is the core resampling function
    // It advances all active streams while writing output samples
    // until we reach the target sample position

    while (currentSample_ < targetSample) {
        // First, check if any streams want to update the DAC value
        for (int i = 0; i < MAX_STREAMS; i++) {
            StreamState& stream = streams_[i];
            if (!stream.active) continue;

            // Only process streams that write to DAC (chipType=0x02, command=0x2A)
            if (stream.chipType != 0x02 || stream.command != 0x2A) continue;

            // Check if this stream should emit a sample at this output position
            // The accumulator tracks fractional progress through stream samples
            stream.accumulator += 1.0f;

            while (stream.accumulator >= stream.samplesPerTick) {
                stream.accumulator -= stream.samplesPerTick;

                // Read next sample from our data bank
                uint32_t absPos = stream.dataStart + stream.dataPos;
                if (absPos < dataBankSize_ && dataBank_ != nullptr) {
                    dacValue_ = dataBank_[absPos];
                } else {
                    dacValue_ = 0x80;  // Silence if out of range
                }

                // Advance stream position
                stream.dataPos += stream.stepSize + 1;

                // Check for end of stream data
                if (stream.dataPos >= stream.dataLength) {
                    if (stream.loop) {
                        stream.dataPos = 0;
                    } else {
                        stream.active = false;
                        break;
                    }
                }
            }
        }

        // Write one sample at 44.1 kHz with current DAC state
        writeSample(dacValue_, panning_, dacEnabled_);
    }
}
