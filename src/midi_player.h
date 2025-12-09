#pragma once
#include <Arduino.h>
#include <IntervalTimer.h>
#include "audio_player_interface.h"
#include "player_config.h"
#include "audio_connection_manager.h"
#include "opl3_synth.h"
#include "file_source.h"
#include "drum_sampler_v2.h"

// Uses streaming MIDI parser (constant RAM usage regardless of file size)
#include "midi_stream.h"
typedef StreamingMidiSong MidiSongImpl;

class MidiPlayer : public IAudioPlayer {
public:
  // Constructor now takes config instead of individual pointers
  explicit MidiPlayer(const PlayerConfig& config);
  ~MidiPlayer() override;

  // ============================================
  // IAudioPlayer INTERFACE IMPLEMENTATION
  // ============================================

  bool loadFile(const char* path) override;
  void play() override;
  void pause() override;
  void resume() override;
  void stop() override;
  void update() override;
  void setCompletionCallback(CompletionCallback callback) override { completionCallback_ = callback; }

  PlayerState getState() const override { return state_; }
  bool isPlaying() const override { return state_ == PlayerState::PLAYING; }
  bool isPaused() const override { return state_ == PlayerState::PAUSED; }
  bool isStopped() const override { return state_ == PlayerState::STOPPED; }

  uint32_t getDurationMs() const override;
  uint32_t getPositionMs() const override;
  float getProgress() const override;

  const char* getFileName() const override { return currentFileName_; }
  FileFormat getFormat() const override { return FileFormat::MIDI; }
  bool isLooping() const override { return false; }

  void printStats() const override;

  // ============================================
  // MIDI-SPECIFIC PUBLIC METHODS (not in interface)
  // ============================================

  void replay();  // Replay the current file from the beginning
  void reset();   // Reset player to initial state (for loading a new file)

  // MIDI file info
  uint16_t getPPQN() const { return midi_.ppqn(); }
  uint32_t getInitialBPM() const { return 60000000 / midi_.initialTempoUSQ(); }
  uint32_t getEventCount() const { return eventCount_; }
  uint32_t getCurrentTick() const { return lastDispatchedTick_; }
  uint32_t getTotalTicks() const { return estimatedTotalTicks_; }
  float getDuration() const;   // Duration in seconds (estimated from current tempo)

  // Drum sampler control (for runtime toggle)
  void setDrumSampler(DrumSamplerV2* drumSampler) { drumSampler_ = drumSampler; }
  DrumSamplerV2* getDrumSampler() const { return drumSampler_; }

private:
  // Timer management
  static MidiPlayer* instance_;  // For ISR callback
  static void onTickISR();
  void startTickTimer(uint32_t us_per_tick);
  void updateTickTimer(uint32_t us_per_tick);
  void stopTickTimer();

  // Event processing
  void processEvents();
  void dispatchEvent(const MidiEvent& ev);

  // Scan file to find total duration (called during load)
  void scanFileDuration();

  // ============================================
  // CONFIGURATION (from PlayerConfig)
  // ============================================
  OPL3Synth* synth_;
  FileSource* fileSource_;
  DrumSamplerV2* drumSampler_;
  AudioMixer4* mixerLeft_;
  AudioMixer4* mixerRight_;
  AudioMixer4* fadeMixerLeft_;
  AudioMixer4* fadeMixerRight_;
  AudioMixer4* finalMixerLeft_;
  AudioMixer4* finalMixerRight_;
  AudioEffectFreeverb* reverbLeft_;
  AudioEffectFreeverb* reverbRight_;
  bool crossfeedEnabled_;
  bool reverbEnabled_;

  // Connection manager (not used by MIDI, but included for consistency)
  AudioConnectionManager connMgr_;

  // ============================================
  // PLAYBACK STATE
  // ============================================
  MidiSongImpl midi_;
  PlayerState state_;
  CompletionCallback completionCallback_;  // Called when playback finishes naturally

  // Timing
  IntervalTimer tickTimer_;
  volatile uint32_t tickCount_;
  uint32_t lastDispatchedTick_;
  uint32_t eventCount_;
  uint32_t estimatedTotalTicks_;  // Estimated from file analysis or updated dynamically

  // Statistics tracking
  uint32_t lastStatsTime_;
  bool showFirstEvents_;

  // Current file info
  char currentFileName_[64];
};