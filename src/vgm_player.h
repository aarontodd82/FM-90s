#pragma once
#include <Arduino.h>
#include <Audio.h>
#include <IntervalTimer.h>
#include "vgm_file.h"
#include "opl3_synth.h"
#include "nes_apu_emulator.h"
#include "gameboy_apu.h"
#include "genesis_board.h"
#include "dac_prerender.h"
#include "audio_stream_dac_prerender.h"
#include "file_source.h"
#include "audio_player_interface.h"
#include "player_config.h"

class VGMPlayer : public IAudioPlayer {
public:
  VGMPlayer(const PlayerConfig& config);
  ~VGMPlayer();

  // IAudioPlayer interface
  bool loadFile(const char* filename) override;
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
  FileFormat getFormat() const override { return FileFormat::VGM; }
  bool isLooping() const override { return loopEnabled_; }
  void printStats() const override;

  // VGM-specific methods
  void reset();
  ChipType getChipType() const { return vgmFile_.getChipType(); }
  uint32_t getTotalSamples() const { return vgmFile_.getTotalSamples(); }
  uint32_t getCurrentSample() const { return sampleCount_; }

private:
  // Timer management
  static VGMPlayer* instance_;
  static void onTimerISR();
  static void onPCMTimerISR();  // High-frequency timer for PCM streaming
  void startTimer();
  void stopTimer();
  void startPCMTimer();
  void stopPCMTimer();

  // Command processing
  void processCommands();
  void processCommand();
  void writeOPL2(uint8_t reg, uint8_t val, uint8_t chip = 0);
  void writeOPL3Port0(uint8_t reg, uint8_t val, uint8_t chip = 0);
  void writeOPL3Port1(uint8_t reg, uint8_t val, uint8_t chip = 0);

  // Delay handling
  void waitSamples(uint32_t samples);
  uint32_t calculateDelayMicros(uint32_t samples);

  // Member variables
  OPL3Synth* synth_;
  NESAPUEmulator* apu_;  // NES APU emulator (dynamically created for NES APU VGMs only)
  GameBoyAPU* gbApu_;    // Game Boy DMG APU emulator (dynamically created for GB VGMs only)
  GenesisBoard* genesisBoard_;  // Genesis synth board (YM2612 + SN76489)
  DACPrerenderer* dacPrerenderer_;  // DAC pre-renderer for Genesis YM2612 PCM playback
  AudioStreamDACPrerender* dacPrerenderStream_;  // Pre-rendered DAC playback stream
  FileSource* fileSource_;  // Note: VGM streaming not yet implemented for USB/Floppy
  VGMFile vgmFile_;
  PlayerState state_;
  CompletionCallback completionCallback_;  // Called when playback finishes naturally

  // Audio routing (from PlayerConfig)
  AudioMixer4* mixerLeft_;        // Submixers for GB APU/SPC/MOD (channel 1 submixer)
  AudioMixer4* mixerRight_;       // Submixers for GB APU/SPC/MOD
  AudioMixer4* dacNesMixerLeft_;  // DAC/NES pre-mixer (ch0=DAC, ch1=NES) - for muting control
  AudioMixer4* dacNesMixerRight_; // DAC/NES pre-mixer (ch0=DAC, ch1=NES) - for muting control
  AudioMixer4* mainMixerLeft_;    // Main mixers for line-in control (channel 0 = hardware)
  AudioMixer4* mainMixerRight_;   // Main mixers for line-in control
  AudioMixer4* fadeMixerLeft_;
  AudioMixer4* fadeMixerRight_;

  // Playback position
  uint32_t sampleCount_;
  uint32_t pendingDelay_;
  bool loopEnabled_;

  // Loop fade-out support
  uint32_t loopCount_;              // Number of loops completed
  bool fadeActive_;                 // True when fading out
  uint32_t fadeStartTime_;          // When fade began (millis)
  uint32_t loopDurationSamples_;    // Duration of one loop (for fade timing)
  uint32_t loopStartSample_;        // Sample count at start of current loop
  bool isFinalLoop_;                // True if we're on the final loop before stopping

  // Timing
  IntervalTimer timer_;
  volatile bool timerFlag_;
  uint32_t nextSampleTime_;   // Microsecond time when next sample is due (integer for micros() comparison)
  double nextSampleTimeF_;    // High-precision accumulator in microseconds (avoids truncation error)
  uint32_t totalCommands_;

  // Current file info
  char currentFileName_[64];

  // Performance measurement
  uint32_t commandsProcessed_;
  uint32_t maxProcessTime_;

  // Genesis support
  bool hasGenesis_;  // True if current VGM contains Genesis chips
  bool useDACPrerender_;  // True to use pre-rendered DAC, false to use hardware DAC
  bool dacPrerendered_;   // True if DAC was successfully pre-rendered for current file
  bool dacCurrentlyEnabled_;  // Tracks current state of DAC (bit 7 of reg 0x2B)

  // Debug counters for Genesis write tracking
  uint32_t debugPsgWrites_;
  uint32_t debugYmPort0Writes_;
  uint32_t debugYmPort1Writes_;
};