#pragma once

#include <Arduino.h>
#include "file_source.h"
#include "audio_player_interface.h"
#include "player_config.h"
#include "audio_stream_spc.h"  // Include the new separate header

// Forward declarations
struct SNES_SPC;
struct SPC_Filter;

class SPCPlayer : public IAudioPlayer {
public:
    SPCPlayer(const PlayerConfig& config);
    ~SPCPlayer();

    // IAudioPlayer interface
    bool loadFile(const char* path) override;
    void play() override;
    void pause() override;
    void resume() override;
    void stop() override;
    void update() override;

    PlayerState getState() const override {
        // Debug output for first few calls
        static int getStateCount = 0;
        if (getStateCount++ < 20) {
            Serial.printf("[SPCPlayer] getState() called, returning %d\n", (int)state_);
        }
        return state_;
    }
    bool isPlaying() const override { return state_ == PlayerState::PLAYING; }
    bool isPaused() const override { return state_ == PlayerState::PAUSED; }
    bool isStopped() const override { return state_ == PlayerState::STOPPED; }

    uint32_t getDurationMs() const override;
    uint32_t getPositionMs() const override;
    float getProgress() const override;
    const char* getFileName() const override { return currentFileName_; }
    FileFormat getFormat() const override { return FileFormat::SPC; }
    bool isLooping() const override { return false; }
    void printStats() const override { /* TODO: implement if needed */ }

    void setCompletionCallback(CompletionCallback callback) override { completionCallback_ = callback; }

    // SPC-specific methods
    void reset();

    // Audio buffer access for AudioStream
    bool fillAudioBuffer(int16_t* left, int16_t* right, size_t samples);

    // SPC-specific functions
    void muteVoice(int voice, bool mute);
    void setTempo(float tempo); // 1.0 = normal, 0.5 = half speed, 2.0 = double speed

    // Metadata access
    const char* getSongTitle() const { return id666_.song_title; }
    const char* getGameTitle() const { return id666_.game_title; }
    const char* getArtist() const { return id666_.artist; }
    const char* getDumper() const { return id666_.dumper; }
    const char* getComments() const { return id666_.comments; }
    bool hasID666Tags() const { return has_id666_; }

    // Get the audio stream for connection
    AudioStreamSPC* getAudioStream() { return audio_stream_; }

private:
    // SPC file structure
    struct SPCHeader {
        char signature[33];     // "SNES-SPC700 Sound File Data v0.30"
        uint8_t tag_format[2];  // 26,26
        uint8_t version[2];     // 10 (version) and ID666 tag present
    };

    // ID666 tag structure
    struct ID666Tag {
        char song_title[33];
        char game_title[33];
        char dumper[17];
        char comments[33];
        char dump_date[12];     // MM/DD/YYYY
        uint32_t seconds_before_fade;
        uint32_t fade_length_ms;
        char artist[33];
        uint8_t default_channel_disabled;
        uint8_t emulator_used;

        // Extended fields (from binary format)
        uint32_t intro_length_ticks;  // 1/64000 second units
        uint32_t loop_length_ticks;
        uint32_t total_length_ticks;
    };

    // Constants
    static constexpr size_t RING_BUFFER_SIZE = 8192;   // Samples per channel
    // AUDIO_BLOCK_SAMPLES is defined by Teensy Audio Library as 128
    // Match example exactly: 2048 total samples (1024 stereo pairs)
    static constexpr size_t SAMPLES_PER_BLOCK = 2048;   // Total samples like example
    static constexpr float SPC_SAMPLE_RATE = 32000.0f;  // Native SPC sample rate
    static constexpr float TEENSY_SAMPLE_RATE = 44100.0f;

    // File management
    FileSource* fileSource_;
    uint8_t* file_data_;
    size_t file_size_;
    char currentFileName_[128];

    // Emulation core
    SNES_SPC* spc_emu_;
    SPC_Filter* filter_;

    // Metadata
    ID666Tag id666_;
    bool has_id666_;

    // Playback state
    PlayerState state_;
    CompletionCallback completionCallback_;  // Called when playback finishes naturally
    uint32_t samples_played_;      // Total samples generated (at 32kHz)
    uint32_t samples_consumed_;    // Total samples consumed (at 44.1kHz)
    uint32_t fade_start_sample_;   // When to start fading
    uint32_t fade_length_samples_; // Fade duration in samples
    uint32_t total_samples_;       // Total duration in samples

    // Audio routing (from PlayerConfig)
    AudioMixer4* mixerLeft_;        // Submixers for SPC (channel 1 submixer)
    AudioMixer4* mixerRight_;       // Submixers for SPC
    AudioMixer4* mainMixerLeft_;    // Main mixers for line-in control (channel 0 = hardware)
    AudioMixer4* mainMixerRight_;   // Main mixers for line-in control
    AudioMixer4* fadeMixerLeft_;
    AudioMixer4* fadeMixerRight_;

    // Audio buffering
    int16_t* ring_buffer_;         // Interleaved stereo buffer
    volatile uint32_t write_pos_;
    volatile uint32_t read_pos_;

    // Resampling state (simple linear interpolation)
    struct {
        float position;     // Current position in source samples
        float increment;    // How much to advance per output sample (32000/44100)
        int16_t prev_l;     // Previous left sample
        int16_t prev_r;     // Previous right sample
    } resampler_;

    // Audio stream
    AudioStreamSPC* audio_stream_;

    // Timer for buffer filling
    IntervalTimer fill_timer_;
    static SPCPlayer* instance_;   // For timer callback

    // Private methods
    bool parseHeader(const uint8_t* data, size_t size);
    bool parseID666Tag(const uint8_t* data, bool text_format);
    void fillBuffer();
    static void fillBufferISR();
    void resampleBuffer(const int16_t* src, int16_t* dst, size_t src_samples, size_t dst_samples);
    void applyFade(int16_t* buffer, size_t samples);
    void calculateDuration();
};

// AudioStreamSPC class moved to audio_stream_spc.h/cpp to avoid ODR violations