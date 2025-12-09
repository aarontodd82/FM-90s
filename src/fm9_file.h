#pragma once

#include <Arduino.h>
#include <SD.h>
#include <cstdint>

// Forward declarations
class FileSource;

/**
 * FM9 Extension Header (24 bytes)
 *
 * Located after VGM data in the gzip-compressed portion of an FM9 file.
 * The audio chunk is stored AFTER the gzip section (uncompressed).
 */
struct FM9Header {
    char     magic[4];        // "FM90"
    uint8_t  version;         // Format version (1)
    uint8_t  flags;           // Bit flags (see FM9_FLAG_*)
    uint8_t  audio_format;    // 0=none, 1=WAV, 2=MP3
    uint8_t  reserved;        // Padding
    uint32_t audio_offset;    // Offset from FM9 header start (not used - audio is after gzip)
    uint32_t audio_size;      // Size of audio data in bytes
    uint32_t fx_offset;       // Offset from FM9 header start to FX data
    uint32_t fx_size;         // Size of FX JSON in bytes
};

// Flag bits
constexpr uint8_t FM9_FLAG_HAS_AUDIO = 0x01;
constexpr uint8_t FM9_FLAG_HAS_FX    = 0x02;
constexpr uint8_t FM9_FLAG_HAS_IMAGE = 0x04;

// Cover image constants
constexpr uint32_t FM9_IMAGE_WIDTH  = 100;
constexpr uint32_t FM9_IMAGE_HEIGHT = 100;
constexpr uint32_t FM9_IMAGE_SIZE   = 100 * 100 * 2;  // 20000 bytes (RGB565)

// Audio format values
constexpr uint8_t FM9_AUDIO_NONE = 0;
constexpr uint8_t FM9_AUDIO_WAV  = 1;
constexpr uint8_t FM9_AUDIO_MP3  = 2;

/**
 * FM9File - Parser for FM9 extended VGM format
 *
 * FM9 file structure:
 * [Gzipped: VGM data + FM9 Header + FX JSON] + [Uncompressed: Audio chunk]
 *
 * The class handles:
 * 1. Streaming gzip decompression of VGM data (same as VGZ)
 * 2. Detection of FM9 header after VGM end command (0x66)
 * 3. Extraction of uncompressed audio to temp file
 * 4. Parsing of FX JSON for effects automation
 */
class FM9File {
public:
    FM9File();
    ~FM9File();

    /**
     * Load FM9 file
     *
     * Scans the file structure:
     * 1. Finds the end of gzip compressed section
     * 2. Extracts audio chunk (if present) to temp file
     * 3. Prepares VGM data for streaming playback
     *
     * @param filename Path to .fm9 file
     * @param fileSource File source abstraction (SD/USB)
     * @return true if loaded successfully
     */
    bool loadFromFile(const char* filename, FileSource* fileSource);

    /**
     * Clear all loaded data and close files
     */
    void clear();

    // ========== FM9 Extension Info ==========

    /**
     * Check if file has FM9 extension header
     */
    bool hasFM9Extension() const { return hasFM9Header_; }

    /**
     * Check if file has embedded audio
     */
    bool hasAudio() const { return hasFM9Header_ && (fm9Header_.flags & FM9_FLAG_HAS_AUDIO); }

    /**
     * Check if file has FX automation data
     */
    bool hasFX() const { return hasFM9Header_ && (fm9Header_.flags & FM9_FLAG_HAS_FX); }

    /**
     * Check if file has cover image (100x100 RGB565)
     */
    bool hasImage() const { return hasFM9Header_ && (fm9Header_.flags & FM9_FLAG_HAS_IMAGE); }

    /**
     * Get byte offset in original file where cover image starts
     * Image is stored after audio chunk (if present)
     */
    uint32_t getImageOffset() const { return gzipEndOffset_ + fm9Header_.audio_size; }

    /**
     * Get audio format (FM9_AUDIO_NONE, FM9_AUDIO_WAV, FM9_AUDIO_MP3)
     */
    uint8_t getAudioFormat() const { return fm9Header_.audio_format; }

    /**
     * Get audio data size in bytes
     */
    uint32_t getAudioSize() const { return fm9Header_.audio_size; }

    /**
     * Get byte offset in original file where audio data starts
     * Audio is stored uncompressed AFTER the gzip section
     */
    uint32_t getAudioOffset() const { return gzipEndOffset_; }

    /**
     * Get FX JSON data (null-terminated)
     * Returns nullptr if no FX data
     */
    const char* getFXJson() const {
        return fxJsonData_ ? fxJsonData_ : nullptr;
    }

    /**
     * Get FX JSON size
     */
    size_t getFXJsonSize() const { return fxJsonSize_; }

    // ========== VGM Data Access ==========

    /**
     * Get offset in original file where gzip section ends
     * Audio data starts at this offset
     */
    uint32_t getGzipEndOffset() const { return gzipEndOffset_; }

    /**
     * Get the original FM9 file path (needed for VGMFile to load VGM portion)
     */
    const char* getOriginalPath() const { return originalPath_; }

private:
    // FM9 extension data
    FM9Header fm9Header_;
    bool hasFM9Header_;

    // FX data (small enough to keep in RAM)
    char* fxJsonData_;
    size_t fxJsonSize_;

    // File info
    char originalPath_[128];   // Original FM9 file path
    uint32_t gzipEndOffset_;   // Where gzip section ends (audio starts)

    // File source
    FileSource* fileSource_;

    // Helper methods
    bool streamDecompressAndParse(File& file);  // Main streaming decompression
    bool parseFM9Header(const uint8_t* data, size_t dataSize);
};
