/**
 * @file audio_stream_fm9_wav.h
 * @brief AudioStream for synchronized WAV playback in FM9 files
 *
 * CRITICAL: This class MUST be in its own translation unit to avoid ODR violations
 * with the Audio Library's update list registration.
 *
 * This stream reads from a WAV file and outputs stereo audio at 44.1 kHz
 * with perfect timing synchronization to the VGM player.
 *
 * Based on AudioStreamDACPrerender pattern - uses ring buffer and sync mechanism.
 *
 * @author Claude + Aaron
 * @date November 2025
 */

#ifndef AUDIO_STREAM_FM9_WAV_H
#define AUDIO_STREAM_FM9_WAV_H

#include <Audio.h>
#include <SD.h>
#include <stdint.h>

/**
 * @class AudioStreamFM9Wav
 * @brief Custom AudioStream for synchronized WAV playback
 *
 * Thread Safety:
 * - update() called by Audio Library ISR at 44.1kHz (every 2.9ms)
 * - Must complete quickly (<2.9ms to avoid ISR overflow)
 * - No blocking operations allowed in update()
 * - Main loop must call refillBuffer() regularly
 *
 * Usage Pattern:
 * 1. Load WAV file with loadFile()
 * 2. Call play() when VGM playback starts
 * 3. Call setTargetSample() from VGMPlayer/FM9Player update loop
 * 4. Call refillBuffer() in main loop
 * 5. Call stop() when playback stops
 */
class AudioStreamFM9Wav : public AudioStream {
public:
    AudioStreamFM9Wav();
    virtual ~AudioStreamFM9Wav();

    // ========== File Management ==========

    /**
     * Load a WAV file for playback
     * @param path Path to WAV file (16-bit stereo 44.1kHz expected)
     * @return true if file loaded successfully
     */
    bool loadFile(const char* path);

    /**
     * Load WAV data from an offset within a larger file (e.g., FM9)
     * This avoids extracting to a temp file - reads directly from the source.
     * @param path Path to the container file (e.g., .fm9)
     * @param audioOffset Byte offset where WAV data begins
     * @param audioSize Size of WAV data in bytes (for bounds checking)
     * @return true if loaded successfully
     */
    bool loadFromOffset(const char* path, uint32_t audioOffset, uint32_t audioSize);

    /**
     * Close the current file
     */
    void closeFile();

    /**
     * Check if a file is loaded
     */
    bool isLoaded() const { return fileLoaded_; }

    // ========== Playback Control ==========

    void play();
    void stop();
    void pause();
    void resume();

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }

    // ========== Position Tracking ==========

    uint32_t getPositionSamples() const { return currentSample_; }
    uint32_t getPositionMs() const { return (currentSample_ * 1000UL) / sampleRate_; }
    uint32_t getTotalSamples() const { return totalSamples_; }
    uint32_t getDurationMs() const { return (totalSamples_ * 1000UL) / sampleRate_; }
    float getProgress() const;

    // ========== Synchronization ==========

    /**
     * Set the target sample position for synchronization
     *
     * Uses gradual rate adjustment instead of sample skipping:
     * - If WAV is behind: plays slightly faster (129 samples -> 128 output)
     * - If WAV is ahead: plays slightly slower (127 samples -> 128 output)
     * - Adjustment is ±1 sample per 128-sample block (~0.78% speed change)
     * - Inaudible pitch shift, no clicks or discontinuities
     *
     * @param targetSample The sample position we should be at
     */
    void setTargetSample(uint32_t targetSample);

    /**
     * Get the current sync drift (difference between actual and target position)
     * Positive = WAV is ahead, Negative = WAV is behind
     */
    int32_t getSyncDrift() const;

    /**
     * Get current sync adjustment mode for diagnostics
     * Returns: -1 = slowing down, 0 = normal, +1 = speeding up
     */
    int8_t getSyncMode() const { return syncMode_; }

    // ========== Buffer Management ==========

    /**
     * Refill read buffer from SD card
     * MUST be called from main loop regularly (every few ms)
     */
    void refillBuffer();

    /**
     * Check if buffer needs refilling
     */
    bool needsRefill() const;

    /**
     * Get current buffer fill level in samples
     */
    size_t getBufferLevel() const;

    // ========== Audio Library Interface ==========

    void update() override;

    // ========== Diagnostics ==========

    uint32_t getUnderruns() const { return underruns_; }
    void resetCounters() { underruns_ = 0; }

    // Prevent copies
    AudioStreamFM9Wav(const AudioStreamFM9Wav&) = delete;
    AudioStreamFM9Wav& operator=(const AudioStreamFM9Wav&) = delete;

private:
    // ========== File State ==========
    File file_;
    bool fileLoaded_;
    uint32_t totalSamples_;
    uint32_t currentSample_;
    uint32_t baseOffset_;       // Offset in file where WAV data starts (0 for standalone WAV)
    uint32_t dataStartOffset_;  // Where PCM data begins (relative to baseOffset_)
    uint32_t sampleRate_;       // Usually 44100
    uint16_t numChannels_;      // 1 or 2
    uint16_t bitsPerSample_;    // 16
    uint16_t bytesPerSample_;   // 2 for 16-bit mono, 4 for 16-bit stereo

    // ========== Playback State ==========
    volatile bool playing_;
    volatile bool paused_;

    // ========== Read Buffer ==========
    // Large buffer in PSRAM for smooth playback despite SD contention
    // 8192 samples = ~186ms of audio at 44.1kHz
    static const size_t BUFFER_SAMPLES = 8192;
    static const size_t REFILL_THRESHOLD = 4096;  // Refill when below this
    int16_t* readBufferLeft_;   // PSRAM allocated
    int16_t* readBufferRight_;  // PSRAM allocated
    volatile size_t bufferReadPos_;
    volatile size_t bufferWritePos_;
    volatile size_t bufferAvailable_;

    // ========== File Read State ==========
    uint32_t fileReadSample_;
    bool endOfFile_;

    // ========== Synchronization ==========
    volatile uint32_t targetSample_;
    volatile bool seekRequested_;        // Flag: main loop needs to seek file
    volatile uint32_t seekTargetSample_; // Sample position to seek to
    volatile uint32_t lastTargetSample_; // Previous target for loop detection
    volatile int8_t syncMode_;           // -1=slow, 0=normal, +1=fast
    volatile bool syncEnabled_;          // False until first setTargetSample() call

    // Drift thresholds for rate adjustment (in samples)
    static const int32_t SYNC_DEAD_ZONE = 64;      // No adjustment within ±64 samples (~1.5ms)
    static const int32_t SYNC_MAX_DRIFT = 4410;    // Force seek if drift exceeds 100ms

    // ========== Diagnostics ==========
    uint32_t underruns_;
    uint32_t seekCount_;                 // Number of seeks performed (for debug)
    uint32_t speedupCount_;              // Times we sped up
    uint32_t slowdownCount_;             // Times we slowed down

    // ========== Private Methods ==========

    bool parseWavHeader();
    size_t available() const;
    size_t space() const;
};

#endif // AUDIO_STREAM_FM9_WAV_H
