#include "fm9_file.h"
#include "file_source.h"
#include "../lib/uzlib/uzlib.h"
#include <string.h>

// Global pointer for streaming callback (same pattern as VGMFile)
static FM9File* g_streamingFM9File = nullptr;
static File* g_streamingFile = nullptr;
static uint8_t* g_compressedBuffer = nullptr;
static const size_t COMPRESSED_BUFFER_SIZE = 4096;

// Callback for uzlib to read more compressed data
// This callback is called when uzlib needs more input data
// CRITICAL: Must check if buffer has data first (same pattern as VGMFile)
static int fm9StreamingReadCallback(struct uzlib_uncomp* uncomp) {
    if (!g_streamingFile || !g_compressedBuffer) {
        return -1;  // EOF
    }

    // Check if we still have buffered compressed data
    if (uncomp->source < uncomp->source_limit) {
        // Return next buffered byte
        return *uncomp->source++;
    }

    // Need to read more compressed data from file
    if (!g_streamingFile->available()) {
        return -1;  // EOF
    }

    int bytesRead = g_streamingFile->read(g_compressedBuffer, COMPRESSED_BUFFER_SIZE);
    if (bytesRead <= 0) {
        return -1;  // EOF or error
    }

    // Update source pointers
    uncomp->source = g_compressedBuffer;
    uncomp->source_limit = g_compressedBuffer + bytesRead;

    // Return first byte
    return *uncomp->source++;
}

FM9File::FM9File()
    : hasFM9Header_(false)
    , fxJsonData_(nullptr)
    , fxJsonSize_(0)
    , gzipEndOffset_(0)
    , fileSource_(nullptr) {
    memset(&fm9Header_, 0, sizeof(fm9Header_));
    memset(originalPath_, 0, sizeof(originalPath_));
}

FM9File::~FM9File() {
    clear();
}

void FM9File::clear() {
    // Free FX JSON data
    if (fxJsonData_) {
        delete[] fxJsonData_;
        fxJsonData_ = nullptr;
    }

    // Reset state
    hasFM9Header_ = false;
    fxJsonSize_ = 0;
    gzipEndOffset_ = 0;
    fileSource_ = nullptr;
    memset(&fm9Header_, 0, sizeof(fm9Header_));
    memset(originalPath_, 0, sizeof(originalPath_));
}

bool FM9File::loadFromFile(const char* filename, FileSource* fileSource) {
    clear();

    if (!filename || !fileSource) {
        Serial.println("[FM9File] Invalid filename or fileSource");
        return false;
    }

    fileSource_ = fileSource;
    strncpy(originalPath_, filename, sizeof(originalPath_) - 1);

    Serial.print("[FM9File] Loading: ");
    Serial.println(filename);

    // Open file
    File file = fileSource_->open(filename, FILE_READ);
    if (!file) {
        Serial.println("[FM9File] Failed to open file");
        return false;
    }

    uint32_t fileSize = file.size();
    Serial.print("[FM9File] File size: ");
    Serial.print(fileSize);
    Serial.println(" bytes");

    // Stream decompress and find FM9 header
    if (!streamDecompressAndParse(file)) {
        Serial.println("[FM9File] Stream decompression failed");
        file.close();
        return false;
    }

    file.close();

    // Log what we found
    if (hasFM9Header_) {
        Serial.println("[FM9File] FM9 header found!");
        Serial.print("  Version: ");
        Serial.println(fm9Header_.version);
        Serial.print("  Flags: 0x");
        Serial.println(fm9Header_.flags, HEX);
        Serial.print("  Audio format: ");
        Serial.println(fm9Header_.audio_format);
        Serial.print("  Audio size: ");
        Serial.println(fm9Header_.audio_size);
        Serial.print("  FX size: ");
        Serial.println(fm9Header_.fx_size);
        Serial.print("  Audio offset: ");
        Serial.println(gzipEndOffset_);
    } else {
        Serial.println("[FM9File] No FM9 header - treating as pure VGZ");
    }

    // No temp file extraction needed - WAV will be streamed directly from FM9 file
    // using AudioStreamFM9Wav::loadFromOffset()

    return true;
}

/**
 * Stream decompress the gzip file and look for FM9 header
 *
 * Uses the same streaming pattern as VGMFile:
 * 1. Uses a callback to refill compressed data
 * 2. Decompresses into a buffer, scans for "FM90" magic
 * 3. Captures FM9Header and FX JSON when found
 * 4. Tracks compressed bytes consumed to find gzip end offset
 */
bool FM9File::streamDecompressAndParse(File& file) {
    const size_t DECOMP_BUF_SIZE = 8192;  // Decompression output buffer
    const size_t DICT_SIZE = 32768;       // Standard gzip dictionary

    // Allocate buffers
    uint8_t* compBuf = new uint8_t[COMPRESSED_BUFFER_SIZE];
    uint8_t* decompBuf = new uint8_t[DECOMP_BUF_SIZE];
    uint8_t* dictBuf = new uint8_t[DICT_SIZE];

    if (!compBuf || !decompBuf || !dictBuf) {
        Serial.println("[FM9File] Failed to allocate decompression buffers");
        delete[] compBuf;
        delete[] decompBuf;
        delete[] dictBuf;
        return false;
    }

    // Set up globals for streaming callback
    g_streamingFM9File = this;
    g_streamingFile = &file;
    g_compressedBuffer = compBuf;

    // Initialize decompressor with dictionary
    struct uzlib_uncomp d;
    memset(&d, 0, sizeof(d));
    uzlib_uncompress_init(&d, dictBuf, DICT_SIZE);

    // Read initial chunk
    file.seek(0);
    int bytesRead = file.read(compBuf, COMPRESSED_BUFFER_SIZE);
    if (bytesRead < 18) {  // Minimum gzip size
        Serial.println("[FM9File] File too small for gzip");
        delete[] compBuf;
        delete[] decompBuf;
        delete[] dictBuf;
        g_streamingFM9File = nullptr;
        g_streamingFile = nullptr;
        g_compressedBuffer = nullptr;
        return false;
    }

    // Set up source with callback for streaming
    d.source = compBuf;
    d.source_limit = compBuf + bytesRead;
    d.source_read_cb = fm9StreamingReadCallback;

    // Set up destination
    d.dest_start = decompBuf;
    d.dest = decompBuf;
    d.dest_limit = decompBuf + DECOMP_BUF_SIZE;

    // Parse gzip header
    int res = uzlib_gzip_parse_header(&d);
    if (res != TINF_OK) {
        Serial.print("[FM9File] Failed to parse gzip header: ");
        Serial.println(res);
        delete[] compBuf;
        delete[] decompBuf;
        delete[] dictBuf;
        g_streamingFM9File = nullptr;
        g_streamingFile = nullptr;
        g_compressedBuffer = nullptr;
        return false;
    }

    Serial.println("[FM9File] Gzip header parsed, scanning for FM9 header...");
    Serial.print("[FM9File] Source after header: ");
    Serial.print((uint32_t)(d.source - compBuf));
    Serial.print(" / ");
    Serial.println(bytesRead);

    // State for FM9 header detection
    uint8_t slideWindow[4] = {0, 0, 0, 0};
    bool foundFM9Magic = false;
    uint8_t headerCapture[sizeof(FM9Header) + 8192];  // Header + room for FX
    size_t headerCapturePos = 0;
    size_t totalDecompressed = 0;
    int loopCount = 0;

    // Stream decompress and scan for FM90 magic
    bool done = false;
    while (!done) {
        loopCount++;
        if (loopCount <= 5 || loopCount % 100 == 0) {
            Serial.print("[FM9File] Decompress loop ");
            Serial.print(loopCount);
            Serial.print(", src offset: ");
            Serial.print((uint32_t)(d.source - compBuf));
            Serial.print(", dest offset: ");
            Serial.println((uint32_t)(d.dest - decompBuf));
        }

        res = uzlib_uncompress(&d);

        // Process decompressed data in buffer
        size_t decompressedBytes = d.dest - decompBuf;

        for (size_t i = 0; i < decompressedBytes; i++) {
            uint8_t byte = decompBuf[i];

            // Update sliding window
            slideWindow[0] = slideWindow[1];
            slideWindow[1] = slideWindow[2];
            slideWindow[2] = slideWindow[3];
            slideWindow[3] = byte;

            // Check for "FM90" magic
            if (!foundFM9Magic &&
                slideWindow[0] == 'F' && slideWindow[1] == 'M' &&
                slideWindow[2] == '9' && slideWindow[3] == '0') {

                foundFM9Magic = true;
                headerCapture[0] = 'F';
                headerCapture[1] = 'M';
                headerCapture[2] = '9';
                headerCapture[3] = '0';
                headerCapturePos = 4;

                Serial.print("[FM9File] Found FM90 magic at decompressed offset ");
                Serial.println(totalDecompressed + i - 3);
            } else if (foundFM9Magic && headerCapturePos < sizeof(headerCapture)) {
                headerCapture[headerCapturePos++] = byte;

                // Once we have the full header, parse it
                if (headerCapturePos == sizeof(FM9Header) && !hasFM9Header_) {
                    memcpy(&fm9Header_, headerCapture, sizeof(FM9Header));
                    hasFM9Header_ = true;

                    Serial.print("[FM9File] FM9 Header parsed. FX size: ");
                    Serial.println(fm9Header_.fx_size);
                }

                // Check if we have everything we need (header + FX)
                if (hasFM9Header_ && headerCapturePos >= sizeof(FM9Header) + fm9Header_.fx_size) {
                    // Extract FX data if present
                    if (fm9Header_.fx_size > 0 && !fxJsonData_) {
                        fxJsonSize_ = fm9Header_.fx_size;
                        fxJsonData_ = new char[fxJsonSize_ + 1];
                        if (fxJsonData_) {
                            memcpy(fxJsonData_, headerCapture + sizeof(FM9Header), fxJsonSize_);
                            fxJsonData_[fxJsonSize_] = '\0';
                            Serial.print("[FM9File] Extracted FX JSON (");
                            Serial.print(fxJsonSize_);
                            Serial.println(" bytes)");
                        }
                    }
                    // We have everything, but continue to find gzip end
                }
            }
        }

        totalDecompressed += decompressedBytes;

        if (res == TINF_DONE) {
            done = true;

            // Calculate where gzip section ends in the original file
            // file.position() gives current read position
            // d.source_limit - d.source gives bytes remaining in buffer
            size_t remainingInBuffer = d.source_limit - d.source;

            // uzlib stops BEFORE the gzip trailer (8 bytes: CRC32 + size)
            // The audio data starts AFTER the trailer
            gzipEndOffset_ = file.position() - remainingInBuffer + 8;  // +8 for gzip trailer

            Serial.print("[FM9File] Decompression complete. Total decompressed: ");
            Serial.print(totalDecompressed);
            Serial.print(" bytes. Gzip data ends at ");
            Serial.print(file.position() - remainingInBuffer);
            Serial.print(", audio starts at ");
            Serial.println(gzipEndOffset_);

        } else if (res == TINF_OK) {
            // Reset destination buffer for next chunk
            // The dictionary ring buffer handles back-references
            d.dest = decompBuf;

        } else {
            Serial.print("[FM9File] Decompression error: ");
            Serial.print(res);
            Serial.print(" (");
            switch (res) {
                case -1: Serial.print("TINF_BUF_ERROR - output full"); break;
                case -2: Serial.print("TINF_CHKSUM_ERROR - checksum mismatch"); break;
                case -3: Serial.print("TINF_DATA_ERROR - invalid data"); break;
                default: Serial.print("unknown"); break;
            }
            Serial.println(")");
            Serial.print("[FM9File] At loop ");
            Serial.print(loopCount);
            Serial.print(", total decompressed: ");
            Serial.print(totalDecompressed);
            Serial.print(", file pos: ");
            Serial.println(file.position());
            delete[] compBuf;
            delete[] decompBuf;
            delete[] dictBuf;
            g_streamingFM9File = nullptr;
            g_streamingFile = nullptr;
            g_compressedBuffer = nullptr;
            return false;
        }
    }

    // Cleanup
    delete[] compBuf;
    delete[] decompBuf;
    delete[] dictBuf;
    g_streamingFM9File = nullptr;
    g_streamingFile = nullptr;
    g_compressedBuffer = nullptr;

    return true;
}

bool FM9File::parseFM9Header(const uint8_t* data, size_t dataSize) {
    if (dataSize < sizeof(FM9Header)) {
        return false;
    }

    // Check magic
    if (data[0] != 'F' || data[1] != 'M' || data[2] != '9' || data[3] != '0') {
        return false;
    }

    memcpy(&fm9Header_, data, sizeof(FM9Header));
    hasFM9Header_ = true;
    return true;
}
