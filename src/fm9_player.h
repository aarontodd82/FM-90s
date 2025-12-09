#pragma once

#include <Arduino.h>
#include <Audio.h>
#include "fm9_file.h"
#include "vgm_player.h"
#include "file_source.h"
#include "audio_player_interface.h"
#include "player_config.h"
#include "fx_engine.h"

/**
 * FM9Player - Player for FM9 extended VGM format
 *
 * FM9 is VGM with optional embedded audio (WAV/MP3) and FX automation.
 * This player wraps VGMPlayer for the VGM portion and adds:
 * - Synchronized WAV/MP3 playback from embedded audio
 * - FX automation timeline for Teensy Audio effects
 *
 * Architecture:
 * - VGMPlayer handles all VGM command processing, timing, chip support
 * - FM9Player adds audio sync and FX on top
 * - No duplication of VGM logic
 *
 * Audio Routing:
 * - VGM → OPL3/NES/GB/Genesis → Line input → Main mixer ch0
 * - WAV → g_fm9WavStream → dacNesMixer ch3 → submixer → main mixer
 * - Both → Fade mixer → Output
 *
 * Synchronization:
 * - Uses AudioStreamFM9Wav with PSRAM ring buffer (~186ms)
 * - FM9Player::update() calls setTargetSample() to sync WAV with VGM
 * - Same pattern as DAC pre-render for Genesis PCM
 *
 * The WAV stream (g_fm9WavStream) and AudioConnections are static globals
 * defined in main.cpp. FM9Player controls playback and gain only.
 */
class FM9Player : public IAudioPlayer {
public:
    explicit FM9Player(const PlayerConfig& config);
    ~FM9Player();

    // IAudioPlayer interface - delegates to VGMPlayer with extensions
    bool loadFile(const char* filename) override;
    void play() override;
    void pause() override;
    void resume() override;
    void stop() override;
    void update() override;
    void setCompletionCallback(CompletionCallback callback) override;

    PlayerState getState() const override;
    bool isPlaying() const override;
    bool isPaused() const override;
    bool isStopped() const override;

    uint32_t getDurationMs() const override;
    uint32_t getPositionMs() const override;
    float getProgress() const override;
    const char* getFileName() const override { return currentFileName_; }
    FileFormat getFormat() const override { return FileFormat::FM9; }
    bool isLooping() const override;
    void printStats() const override;

    // FM9-specific info
    bool hasAudio() const { return fm9File_.hasAudio(); }
    bool hasFX() const { return fm9File_.hasFX(); }
    bool hasImage() const { return fm9File_.hasImage(); }
    uint8_t getAudioFormat() const { return fm9File_.getAudioFormat(); }
    ChipType getChipType() const;

    // Cover image access (100x100 RGB565, 20000 bytes)
    // Returns nullptr if no image or not loaded
    const uint16_t* getCoverImage() const { return coverImage_; }
    bool hasCoverImage() const { return coverImage_ != nullptr; }

private:
    // Audio synchronization
    void startAudioPlayback();
    void stopAudioPlayback();
    void pauseAudioPlayback();
    void resumeAudioPlayback();
    void updateFXEngine();

    // Cover image loading
    bool loadCoverImage(const char* filename);
    void freeCoverImage();

    // Member variables
    PlayerConfig config_;
    FileSource* fileSource_;

    // Cover image (100x100 RGB565, stored in PSRAM if available)
    uint16_t* coverImage_;  // nullptr if no image

    // Core players
    VGMPlayer* vgmPlayer_;      // Handles all VGM playback (owned)
    FM9File fm9File_;           // FM9 extension parsing
    FXEngine fxEngine_;         // FX automation (skeleton for now)

    // Audio state (WAV/MP3 streams are global g_fm9WavStream/g_fm9Mp3Stream)
    bool audioPlaying_;

    // Audio routing - two-level mixer architecture:
    // 1. fm9AudioMixer: combines WAV (ch0) and MP3 (ch1) - mutually exclusive
    // 2. dacNesMixer: fm9AudioMixer output goes to channel 3
    AudioMixer4* fm9AudioMixerLeft_;   // FM9 WAV/MP3 pre-mixer
    AudioMixer4* fm9AudioMixerRight_;
    AudioMixer4* dacNesMixerLeft_;     // Parent mixer (FM9 output on ch3)
    AudioMixer4* dacNesMixerRight_;

    // FM9 audio mixer channels
    static const int FM9_WAV_CHANNEL = 0;   // WAV on fm9AudioMixer channel 0
    static const int FM9_MP3_CHANNEL = 1;   // MP3 on fm9AudioMixer channel 1
    static const int FM9_DAC_CHANNEL = 3;   // FM9 pre-mixer output on dacNesMixer channel 3
    float audioGain_;  // Audio playback gain (default 0.8)

    // File info
    char currentFileName_[64];

    // Completion callback
    CompletionCallback completionCallback_;
    bool completionFired_;  // Ensure callback only fires once
};
