#pragma once
#include <Arduino.h>
#include <vector>
#include <cstdint>
#include <SPI.h>
#include "opl3_duo_logged.h"

// Pins for OPL3 Duo
struct OPL3Pins {
  uint8_t latchWR;
  uint8_t resetIC;
  uint8_t addrA0;
  uint8_t addrA1;
  uint8_t addrA2;
  uint8_t spiMOSI;
  uint8_t spiSCK;
};

// MIDI channel state
struct ChannelState {
  uint8_t program = 0;
  float volume = 1.0f;           // Logarithmic, 0.0-1.0
  uint8_t pan = 64;
  bool sustain = false;
  int16_t pitchBend = 0;
  uint8_t pbRange = 2;

  // Store loaded instruments in memory (not read from chip)
  Instrument instrument2op;
  Instrument4OP instrument4op;
};

// Voice types
enum VoiceType {
  VOICE_FREE,
  VOICE_2OP,
  VOICE_4OP
};

// Voice allocation info
struct Voice {
  VoiceType type = VOICE_FREE;
  uint8_t midiCh = 0;
  uint8_t midiKey = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  bool pendingOff = false;

  // For 4-op voices: the 4-op channel index (0-11)
  // For 2-op voices: the physical channel index (0-35)
  uint8_t oplChannel = 0;

  // For drums: store the transpose value from the drum instrument
  uint8_t drumTranspose = 0;
};

class OPL3Synth {
public:
  virtual ~OPL3Synth() { if (opl) delete opl; }

  void begin(const OPL3Pins& pins);
  void resetAll();
  void allNotesOff();
  void hardwareReset();  // Full hardware reset of OPL3 chips using library's reset()

  // Force all voices to use 2-op (disable 4-op)
  void setForce2OpMode(bool enable) { force2OpOnly_ = enable; }
  bool isForce2OpMode() const { return force2OpOnly_; }

  // Set maximum concurrent 4-op voices (1-12)
  void setMax4OpVoices(uint8_t max) { max4OpVoices_ = min(max, (uint8_t)12); }
  uint8_t getMax4OpVoices() const { return max4OpVoices_; }

  // Set whether drum sampler is enabled (frees drum channels for melodic use)
  void setDrumSamplerEnabled(bool enabled) { drumSamplerEnabled_ = enabled; }
  bool isDrumSamplerEnabled() const { return drumSamplerEnabled_; }

  void noteOn(uint8_t ch, uint8_t key, uint8_t vel, uint32_t tick = 0);
  void noteOff(uint8_t ch, uint8_t key, uint8_t vel);
  void programChange(uint8_t ch, uint8_t pg);
  void controlChange(uint8_t ch, uint8_t cc, uint8_t val);
  void pitchBend(uint8_t ch, int16_t bend);
  void channelPressure(uint8_t ch, uint8_t value);

  // Debug/stats
  uint8_t getVoicesUsed() const;
  void printVoiceStats() const;

  // Direct OPL3 access for VGM player
  OPL3Duo* getOPL() { return opl; }

private:
  OPL3Duo* opl = nullptr;
  ChannelState ch_[16];

  static constexpr uint8_t MAX_VOICES = 30;  // Reserve some for drums
  static constexpr uint8_t NUM_DRUM_CHANNELS = 6;

  Voice voices_[MAX_VOICES];
  bool force2OpOnly_ = false;     // Runtime flag to disable 4-op
  uint8_t max4OpVoices_ = 2;      // Max concurrent 4-op voices (configurable)
  bool drumSamplerEnabled_ = false;  // When true, drum channels available for melodic use

  // Drum channels: specific physical channels not in 4-op pairs
  const uint8_t drumChannels_[NUM_DRUM_CHANNELS] = {6, 7, 8, 15, 16, 17};

  // Helper functions
  int allocateVoice(uint8_t midiCh, uint8_t key, bool want4op);
  void freeVoice(int vid);
  uint8_t count4opVoices();
  bool prefer4opForProgram(uint8_t program);

  void applyInstrument(Voice& v, uint8_t midiCh);
  void applyVolume(Voice& v, uint8_t midiCh);
  void applyPitch(Voice& v, uint8_t midiCh, int16_t bend);
  void applyPanning(Voice& v, uint8_t midiCh);

  int allocateDrumChannel();
  uint8_t applyDrumInstrument(uint8_t physCh, uint8_t noteNum, uint8_t velocity);
};
