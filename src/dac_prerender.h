/**
 * @file dac_prerender.h
 * @brief Pre-renderer for YM2612 DAC audio in Genesis VGM files
 *
 * Solves timing issues with dense PCM playback by expanding all DAC commands
 * to a linear 44.1 kHz sample stream before playback begins.
 *
 * The pre-rendered file format:
 *   Header (16 bytes):
 *     - Magic "DAC1" (4 bytes)
 *     - Total samples (4 bytes, uint32_t)
 *     - Loop point sample (4 bytes, uint32_t, 0xFFFFFFFF if no loop)
 *     - Flags (4 bytes, reserved)
 *
 *   Data (totalSamples * 2 bytes):
 *     For each sample:
 *       - Byte 0: DAC sample value (8-bit unsigned)
 *       - Byte 1: Flags
 *           Bits 7-6: Panning (00=mute, 01=right, 10=left, 11=center)
 *           Bit 5: DAC enabled (1=yes, 0=no)
 *           Bits 4-0: Reserved
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#pragma once

#include <Arduino.h>
#include <SD.h>

// Forward declarations
class VGMFile;

/**
 * @class DACPrerenderer
 * @brief Pre-renders YM2612 DAC stream from VGM file to linear sample file
 *
 * Handles all DAC-related VGM commands:
 * - 0x52 0x2A: Direct DAC write
 * - 0x52 0x2B: DAC enable/disable
 * - 0x53 0xB6: Channel 6 panning
 * - 0x8n: Data bank read + wait
 * - 0xE0: Data bank seek
 * - 0x67: Data block load (type 0x00)
 * - 0x90-0x95: Stream control (variable sample rates)
 */
class DACPrerenderer {
public:
    DACPrerenderer();
    ~DACPrerenderer();

    /**
     * Pre-render a VGM file's DAC stream to a file
     * @param vgmFile Loaded VGM file to pre-render
     * @param outputPath Path to write pre-rendered file (e.g., "/TEMP/~dac.tmp")
     * @return true if successful, false on error
     */
    bool preRender(VGMFile* vgmFile, const char* outputPath);

    /**
     * Get error message if preRender() failed
     * @return Error message string, or nullptr if no error
     */
    const char* getError() const { return error_; }

    /**
     * Get the total number of samples in the last pre-rendered file
     * @return Total samples, or 0 if no file rendered
     */
    uint32_t getTotalSamples() const { return totalSamplesRendered_; }

    /**
     * Get the loop point sample position
     * @return Loop point sample, or 0xFFFFFFFF if no loop
     */
    uint32_t getLoopPointSample() const { return loopPointSample_; }

    /**
     * Progress callback type
     * @param progress Progress from 0.0 to 1.0
     * @param userData User-provided context pointer
     */
    typedef void (*ProgressCallback)(float progress, void* userData);

    /**
     * Set progress callback for long pre-render operations
     * @param callback Function to call with progress updates
     * @param userData Context pointer passed to callback
     */
    void setProgressCallback(ProgressCallback callback, void* userData);

    // ========== File Format Constants ==========

    static const uint32_t MAGIC = 0x31434144;  // "DAC1" in little-endian
    static const size_t HEADER_SIZE = 16;
    static const uint32_t NO_LOOP = 0xFFFFFFFF;

    // Flag byte bit definitions
    static const uint8_t FLAG_PAN_MASK = 0xC0;      // Bits 7-6
    static const uint8_t FLAG_PAN_MUTE = 0x00;      // 00xxxxxx = muted
    static const uint8_t FLAG_PAN_RIGHT = 0x40;    // 01xxxxxx = right only
    static const uint8_t FLAG_PAN_LEFT = 0x80;     // 10xxxxxx = left only
    static const uint8_t FLAG_PAN_CENTER = 0xC0;   // 11xxxxxx = center (both)
    static const uint8_t FLAG_DAC_ENABLED = 0x20;  // Bit 5

private:
    // ========== Pre-render State ==========

    // Current DAC state (updated as commands are processed)
    uint8_t dacValue_;          // Current DAC sample value (8-bit unsigned)
    bool dacEnabled_;           // DAC enable state (register 0x2B bit 7)
    uint8_t panning_;           // Current panning register value (0xB6)
    uint32_t currentSample_;    // Current sample position in output

    // Data bank (PCM samples loaded via 0x67 command)
    // Pre-renderer manages its own data bank - does NOT rely on VGMFile
    static const size_t MAX_DATA_BANK_SIZE = 262144;  // 256KB max for PCM data
    uint8_t* dataBank_;         // Our own data bank (allocated in PSRAM or heap)
    uint32_t dataBankSize_;     // Current size of data in bank
    uint32_t dataBankCapacity_; // Allocated capacity
    uint32_t dataBankPos_;      // Current read position in data bank
    bool dataBankInPSRAM_;      // True if dataBank_ was allocated in PSRAM (use extmem_free)

    // Stream control state (commands 0x90-0x95)
    struct StreamState {
        bool active;            // Is stream currently playing?
        uint8_t chipType;       // Chip type (0x02 = YM2612)
        uint8_t port;           // Port to write to
        uint8_t command;        // Register/command (0x2A for DAC)
        uint8_t dataBankID;     // Data bank ID (usually 0)
        uint8_t stepSize;       // Bytes to skip after each read (usually 0)
        uint32_t frequency;     // Sample rate in Hz
        uint32_t dataStart;     // Start offset in data bank
        uint32_t dataLength;    // Length of stream data
        uint32_t dataPos;       // Current position in stream data
        bool loop;              // Loop when reaching end?

        // Resampling state
        float accumulator;      // Fractional sample accumulator
        float samplesPerTick;   // 44100 / frequency
    };
    static const int MAX_STREAMS = 4;
    StreamState streams_[MAX_STREAMS];

    // Output file
    File outputFile_;
    uint32_t totalSamplesRendered_;
    uint32_t loopPointSample_;

    // Error handling
    const char* error_;

    // Progress callback
    ProgressCallback progressCallback_;
    void* progressUserData_;
    uint32_t lastProgressUpdate_;

    // Write buffer (reduces SD card write overhead)
    static const size_t WRITE_BUFFER_SIZE = 4096;  // 2048 samples
    uint8_t writeBuffer_[WRITE_BUFFER_SIZE];
    size_t writeBufferPos_;

    // ========== Private Methods ==========

    /**
     * Reset all state for a new pre-render
     */
    void resetState();

    /**
     * Write file header
     * @param totalSamples Total number of samples
     * @param loopPoint Loop point sample position
     * @return true if successful
     */
    bool writeHeader(uint32_t totalSamples, uint32_t loopPoint);

    /**
     * Update header with final values (called after rendering)
     * @return true if successful
     */
    bool updateHeader();

    /**
     * Flush write buffer to file
     * @return true if successful
     */
    bool flushWriteBuffer();

    /**
     * Write a single sample to output
     * @param dacValue 8-bit unsigned DAC value
     * @param panning Panning register value
     * @param dacEnabled DAC enable state
     */
    void writeSample(uint8_t dacValue, uint8_t panning, bool dacEnabled);

    /**
     * Write multiple identical samples (for wait commands)
     * @param count Number of samples to write
     */
    void writeSamples(uint32_t count);

    /**
     * Convert panning register (0xB6) to flag bits
     * @param panReg Panning register value
     * @return Flag byte with panning bits set
     */
    uint8_t panningToFlags(uint8_t panReg) const;

    /**
     * Process a single VGM command
     * @param vgmFile VGM file to read from
     * @param cmd Command byte
     * @return true to continue, false if end of data or error
     */
    bool processCommand(VGMFile* vgmFile, uint8_t cmd);

    /**
     * Process stream control commands (0x90-0x95)
     */
    void processStreamSetup(VGMFile* vgmFile);      // 0x90
    void processStreamData(VGMFile* vgmFile);       // 0x91
    void processStreamFrequency(VGMFile* vgmFile);  // 0x92
    void processStreamStart(VGMFile* vgmFile);      // 0x93
    void processStreamStop(VGMFile* vgmFile);       // 0x94
    void processStreamFast(VGMFile* vgmFile);       // 0x95

    /**
     * Update all active streams to target sample position
     * Handles resampling from stream frequency to 44.1 kHz
     * @param targetSample Target sample position
     */
    void updateStreamsToSample(uint32_t targetSample);

    /**
     * Skip bytes for unhandled commands
     * @param vgmFile VGM file to read from
     * @param cmd Command byte
     */
    void skipCommand(VGMFile* vgmFile, uint8_t cmd);

    /**
     * Report progress if callback is set
     * @param current Current sample position
     * @param total Total samples
     */
    void reportProgress(uint32_t current, uint32_t total);
};
