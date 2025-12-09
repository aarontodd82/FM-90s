/**
 * @file audio_stream_fm9_mp3.h
 * @brief AudioStream for synchronized MP3 playback in FM9 files
 *
 * CRITICAL: This class MUST be in its own translation unit to avoid ODR violations
 * with the Audio Library's update list registration.
 *
 * This stream reads MP3 from an FM9 file and outputs stereo audio at 44.1 kHz
 * with perfect timing synchronization to the VGM player.
 *
 * Uses the Helix MP3 decoder (fixed-point, ARM-optimized).
 * Based on AudioStreamFM9Wav pattern - uses ring buffer and sync mechanism.
 *
 * Key difference from WAV: MP3 decodes in frames (1152 samples), so we apply
 * the rate adjustment trick (127/128/129 samples) AFTER decoding to the ring buffer.
 *
 * @author Claude + Aaron
 * @date December 2025
 */

#ifndef AUDIO_STREAM_FM9_MP3_H
#define AUDIO_STREAM_FM9_MP3_H

#include <Audio.h>
#include <SD.h>
#include <stdint.h>

// Forward declare Helix types to avoid including the full header
typedef void *HMP3Decoder;
struct _MP3FrameInfo;
typedef struct _MP3FrameInfo MP3FrameInfo;

/**
 * @class AudioStreamFM9Mp3
 * @brief Custom AudioStream for synchronized MP3 playback
 *
 * Thread Safety:
 * - update() called by Audio Library ISR at 44.1kHz (every 2.9ms)
 * - Must complete quickly (<2.9ms to avoid ISR overflow)
 * - No blocking operations allowed in update()
 * - Main loop must call refillBuffer() regularly (does decoding)
 *
 * Usage Pattern:
 * 1. Load MP3 file with loadFromOffset()
 * 2. Call play() when VGM playback starts
 * 3. Call setTargetSample() from FM9Player update loop
 * 4. Call refillBuffer() in main loop (does actual MP3 decoding)
 * 5. Call stop() when playback stops
 *
 * Memory Requirements:
 * - Helix decoder state: ~30KB (internal allocation)
 * - Frame buffer: 2KB (compressed MP3 data)
 * - Decoded PCM buffer: 32KB in PSRAM (16KB left + 16KB right)
 */
class AudioStreamFM9Mp3 : public AudioStream {
public:
    AudioStreamFM9Mp3();
    virtual ~AudioStreamFM9Mp3();

    // ========== File Management ==========

    /**
     * Load MP3 data from an offset within a larger file (e.g., FM9)
     * @param path Path to the container file (e.g., .fm9)
     * @param mp3Offset Byte offset where MP3 data begins
     * @param mp3Size Size of MP3 data in bytes
     * @return true if loaded successfully
     */
    bool loadFromOffset(const char* path, uint32_t mp3Offset, uint32_t mp3Size);

    /**
     * Close the current file and release decoder
     */
    void closeFile();

    /**
     * Check if a file is loaded and decoder initialized
     */
    bool isLoaded() const { return fileLoaded_ && decoder_ != nullptr; }

    // ========== Playback Control ==========

    void play();
    void stop();
    void pause();
    void resume();

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }

    // ========== Position Tracking ==========

    uint32_t getPositionSamples() const { return currentSample_; }
    uint32_t getPositionMs() const { return (currentSample_ * 1000UL) / 44100; }
    uint32_t getTotalSamples() const { return totalSamples_; }
    uint32_t getDurationMs() const { return (totalSamples_ * 1000UL) / 44100; }
    float getProgress() const;

    // ========== Synchronization ==========

    /**
     * Set the target sample position for synchronization
     *
     * Uses gradual rate adjustment (same as WAV):
     * - If MP3 is behind: plays slightly faster (129 samples -> 128 output)
     * - If MP3 is ahead: plays slightly slower (127 samples -> 128 output)
     * - Inaudible pitch shift, no clicks or discontinuities
     *
     * @param targetSample The sample position we should be at
     */
    void setTargetSample(uint32_t targetSample);

    /**
     * Get the current sync drift (difference between actual and target position)
     * Positive = MP3 is ahead, Negative = MP3 is behind
     */
    int32_t getSyncDrift() const;

    /**
     * Get current sync adjustment mode for diagnostics
     * Returns: -1 = slowing down, 0 = normal, +1 = speeding up
     */
    int8_t getSyncMode() const { return syncMode_; }

    // ========== Buffer Management ==========

    /**
     * Refill decoded PCM buffer by decoding MP3 frames
     * MUST be called from main loop regularly (does actual decoding work)
     */
    void refillBuffer();

    /**
     * Check if buffer needs refilling
     */
    bool needsRefill() const;

    /**
     * Get current decoded buffer fill level in samples
     */
    size_t getBufferLevel() const;

    // ========== Audio Library Interface ==========

    void update() override;

    // ========== Diagnostics ==========

    uint32_t getUnderruns() const { return underruns_; }
    uint32_t getDecodeErrors() const { return decodeErrors_; }
    void resetCounters() { underruns_ = 0; decodeErrors_ = 0; }

    // Prevent copies
    AudioStreamFM9Mp3(const AudioStreamFM9Mp3&) = delete;
    AudioStreamFM9Mp3& operator=(const AudioStreamFM9Mp3&) = delete;

private:
    // ========== File State ==========
    File file_;
    bool fileLoaded_;
    uint32_t baseOffset_;       // Where MP3 data starts in file
    uint32_t mp3Size_;          // Total MP3 data size
    uint32_t fileReadPos_;      // Current read position in MP3 stream (relative to baseOffset_)
    uint32_t totalSamples_;     // Estimated total samples (from duration)
    uint32_t currentSample_;    // Current output sample position

    // ========== Decoder State ==========
    HMP3Decoder decoder_;       // Helix decoder handle
    MP3FrameInfo* frameInfo_;   // Last decoded frame info

    // ========== MP3 Frame Buffer ==========
    // Buffer for compressed MP3 data read from SD
    static const size_t FRAME_BUFFER_SIZE = 2048;  // Max MP3 frame ~1440 bytes
    uint8_t* frameBuffer_;      // Compressed data buffer
    size_t frameBufferFill_;    // Bytes currently in frame buffer
    size_t frameBufferReadPos_; // Read position in frame buffer

    // ========== Decoded PCM Ring Buffer ==========
    // Large buffer in PSRAM for smooth playback
    // 8192 samples = ~186ms of audio at 44.1kHz
    static const size_t BUFFER_SAMPLES = 8192;
    static const size_t REFILL_THRESHOLD = 4096;  // Refill when below this
    int16_t* decodedBufferLeft_;   // PSRAM allocated
    int16_t* decodedBufferRight_;  // PSRAM allocated
    volatile size_t bufferReadPos_;
    volatile size_t bufferWritePos_;
    volatile size_t bufferAvailable_;

    // ========== Playback State ==========
    volatile bool playing_;
    volatile bool paused_;
    bool endOfFile_;

    // ========== Synchronization ==========
    volatile uint32_t targetSample_;
    volatile bool seekRequested_;        // Flag: main loop needs to seek file
    volatile uint32_t seekTargetSample_; // Sample position to seek to
    volatile uint32_t lastTargetSample_; // Previous target for loop detection
    volatile int8_t syncMode_;           // -1=slow, 0=normal, +1=fast
    volatile bool syncEnabled_;          // False until first setTargetSample() call

    // Drift thresholds (same as WAV)
    static const int32_t SYNC_DEAD_ZONE = 64;      // No adjustment within ±64 samples (~1.5ms)
    static const int32_t SYNC_MAX_DRIFT = 4410;    // Force seek if drift exceeds 100ms

    // ========== Diagnostics ==========
    uint32_t underruns_;
    uint32_t decodeErrors_;
    uint32_t seekCount_;
    uint32_t speedupCount_;
    uint32_t slowdownCount_;
    uint32_t totalDecodedSamples_;  // Total samples decoded (for position tracking)

    // ========== Private Methods ==========

    bool initDecoder();
    void freeDecoder();
    bool decodeNextFrame();       // Decode one MP3 frame to ring buffer
    bool seekToSample(uint32_t sample);  // Seek to approximate position
    bool fillFrameBuffer();       // Read compressed data from SD
    size_t available() const;
    size_t space() const;
};

#endif // AUDIO_STREAM_FM9_MP3_H
