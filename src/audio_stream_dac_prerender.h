/**
 * @file audio_stream_dac_prerender.h
 * @brief AudioStream for playback of pre-rendered Genesis DAC audio
 *
 * CRITICAL: This class MUST be in its own translation unit to avoid ODR violations
 * with the Audio Library's update list registration. Pattern follows AudioStreamSPC.
 *
 * This stream reads from a pre-rendered DAC file (created by DACPrerenderer)
 * and outputs stereo audio at 44.1 kHz with perfect timing synchronization.
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#ifndef AUDIO_STREAM_DAC_PRERENDER_H
#define AUDIO_STREAM_DAC_PRERENDER_H

#include <Audio.h>
#include <SD.h>
#include <stdint.h>

// Forward declaration
class DACPrerenderer;

/**
 * @class AudioStreamDACPrerender
 * @brief Custom AudioStream for pre-rendered Genesis DAC playback
 *
 * Thread Safety:
 * - update() called by Audio Library ISR at 44.1kHz (every 2.9ms)
 * - Must complete quickly (<2.9ms to avoid ISR overflow)
 * - No blocking operations allowed in update()
 * - Main loop must call refillBuffer() regularly
 *
 * Usage Pattern:
 * 1. Pre-render DAC with DACPrerenderer::preRender()
 * 2. Load pre-rendered file with loadFile()
 * 3. Call play() when VGM playback starts
 * 4. Call refillBuffer() in main loop
 * 5. Call stop() when VGM playback stops
 */
class AudioStreamDACPrerender : public AudioStream {
public:
    /**
     * Constructor
     */
    AudioStreamDACPrerender();

    /**
     * Destructor
     */
    virtual ~AudioStreamDACPrerender();

    // ========== File Management ==========

    /**
     * Load a pre-rendered DAC file
     * @param path Path to .dac file created by DACPrerenderer
     * @return true if file loaded successfully
     */
    bool loadFile(const char* path);

    /**
     * Close the current file
     */
    void closeFile();

    /**
     * Check if a file is loaded
     * @return true if file is loaded and valid
     */
    bool isLoaded() const { return fileLoaded_; }

    // ========== Playback Control ==========

    /**
     * Start playback from beginning
     */
    void play();

    /**
     * Stop playback
     */
    void stop();

    /**
     * Pause playback (holds current position)
     */
    void pause();

    /**
     * Resume playback from paused position
     */
    void resume();

    /**
     * Check if currently playing
     * @return true if playing (not paused or stopped)
     */
    bool isPlaying() const { return playing_ && !paused_; }

    /**
     * Check if paused
     * @return true if paused
     */
    bool isPaused() const { return paused_; }

    // ========== Loop Control ==========

    /**
     * Enable or disable looping
     * @param enable true to loop, false to stop at end
     */
    void setLoopEnabled(bool enable) { loopEnabled_ = enable; }

    /**
     * Check if looping is enabled
     * @return true if looping
     */
    bool isLoopEnabled() const { return loopEnabled_; }

    /**
     * Seek to loop point
     * Called automatically when reaching end with looping enabled
     */
    void seekToLoop();

    // ========== Position Tracking ==========

    /**
     * Get current sample position
     * @return Current sample number (0 to totalSamples-1)
     */
    uint32_t getPositionSamples() const { return currentSample_; }

    /**
     * Get current position in milliseconds
     * @return Current position in ms
     */
    uint32_t getPositionMs() const { return (currentSample_ * 1000UL) / 44100UL; }

    /**
     * Get total samples in file
     * @return Total sample count
     */
    uint32_t getTotalSamples() const { return totalSamples_; }

    /**
     * Get total duration in milliseconds
     * @return Total duration in ms
     */
    uint32_t getDurationMs() const { return (totalSamples_ * 1000UL) / 44100UL; }

    /**
     * Get playback progress
     * @return Progress from 0.0 to 1.0
     */
    float getProgress() const;

    /**
     * Seek to specific sample position
     * @param sample Sample position to seek to
     * @return true if seek successful
     */
    bool seekToSample(uint32_t sample);

    /**
     * Set the target sample position for synchronization
     * The ISR will skip or repeat samples to stay aligned with this target.
     * Call this from the VGM player's update loop with the current sampleCount_.
     * @param targetSample The sample position we should be at
     */
    void setTargetSample(uint32_t targetSample);

    /**
     * Get the current sync drift (difference between actual and target position)
     * Positive = DAC is ahead, Negative = DAC is behind
     * @return Drift in samples
     */
    int32_t getSyncDrift() const;

    // ========== Buffer Management ==========

    /**
     * Refill read buffer from SD card
     * MUST be called from main loop regularly (every few ms)
     * Not called from ISR - safe for SD card access
     */
    void refillBuffer();

    /**
     * Check if buffer needs refilling
     * @return true if buffer is below threshold
     */
    bool needsRefill() const;

    /**
     * Get current buffer fill level
     * @return Number of samples in buffer
     */
    size_t getBufferLevel() const;

    // ========== Audio Library Interface ==========

    /**
     * Audio Library ISR callback
     * Called at 44.1kHz to fill audio blocks (128 samples each)
     * CRITICAL: No SD card access or Serial.print in this function!
     */
    void update() override;

    // ========== Diagnostics ==========

    /**
     * Get number of buffer underruns
     * @return Underrun count (buffer empty when ISR needed samples)
     */
    uint32_t getUnderruns() const { return underruns_; }

    /**
     * Reset diagnostic counters
     */
    void resetCounters() { underruns_ = 0; }

    // Prevent copies (critical for Audio Library registration)
    AudioStreamDACPrerender(const AudioStreamDACPrerender&) = delete;
    AudioStreamDACPrerender& operator=(const AudioStreamDACPrerender&) = delete;

private:
    // ========== File State ==========
    File file_;
    bool fileLoaded_;
    uint32_t totalSamples_;
    uint32_t loopPointSample_;
    uint32_t currentSample_;

    // ========== Playback State ==========
    volatile bool playing_;
    volatile bool paused_;
    bool loopEnabled_;

    // ========== Read Buffer ==========
    // Sized to hold enough samples for smooth playback
    // 1024 samples = ~23ms of audio, refill when below 512 (~11.6ms)
    static const size_t BUFFER_SAMPLES = 1024;
    static const size_t REFILL_THRESHOLD = 512;
    uint8_t readBuffer_[BUFFER_SAMPLES * 2];  // 2 bytes per sample
    volatile size_t bufferReadPos_;   // Read position (ISR)
    volatile size_t bufferWritePos_;  // Write position (main loop)
    volatile size_t bufferAvailable_; // Samples available to read

    // ========== File Read State ==========
    uint32_t fileReadSample_;  // Next sample to read from file
    bool endOfFile_;           // Reached end of file
    volatile bool needsSeek_;  // ISR signals main loop to seek file (for looping)

    // ========== Synchronization ==========
    volatile uint32_t targetSample_;  // Target position from VGM player

    // ========== Diagnostics ==========
    uint32_t underruns_;

    // ========== Private Methods ==========

    /**
     * Read and validate file header
     * @return true if header is valid
     */
    bool readHeader();

    /**
     * Get number of samples available in ring buffer
     * @return Available samples
     */
    size_t available() const;

    /**
     * Get free space in ring buffer
     * @return Free sample slots
     */
    size_t space() const;
};

#endif // AUDIO_STREAM_DAC_PRERENDER_H
