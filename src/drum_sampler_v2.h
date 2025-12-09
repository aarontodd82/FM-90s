#ifndef DRUM_SAMPLER_V2_H
#define DRUM_SAMPLER_V2_H

#include <Arduino.h>
#include <Audio.h>

// Number of drum voices (polyphony)
#define DRUM_VOICES 8

class DrumSamplerV2 {
public:
  DrumSamplerV2();
  ~DrumSamplerV2();

  bool begin();

  // MIDI handlers
  void noteOn(uint8_t midiNote, uint8_t velocity);
  void noteOff(uint8_t midiNote);

  // Must be called regularly to manage voice cleanup
  void update();

  // Configuration
  void setEnabled(bool enabled) { enabled_ = enabled; }
  bool isEnabled() const { return enabled_; }

  // Audio outputs (stereo)
  AudioMixer4& getOutputLeft() { return leftFinal_; }
  AudioMixer4& getOutputRight() { return rightFinal_; }

  // Statistics
  void printStatistics();

private:
  // Voice structure
  struct Voice {
    AudioPlayMemory* player;
    AudioEffectFade* fade;      // Prevents clicks when starting/stopping
    uint8_t midiNote;
    uint32_t startTime;
    uint32_t fadeOutTime;       // When to trigger fadeOut (0 = no fadeout scheduled)
    bool active;
  };

  // Audio components
  Voice voices_[DRUM_VOICES];

  // Stereo mixer architecture
  AudioMixer4 leftMixer1_;   // Voices 0-3 left channel
  AudioMixer4 leftMixer2_;   // Voices 4-7 left channel
  AudioMixer4 rightMixer1_;  // Voices 0-3 right channel
  AudioMixer4 rightMixer2_;  // Voices 4-7 right channel
  AudioMixer4 leftFinal_;    // Left output
  AudioMixer4 rightFinal_;   // Right output

  AudioConnection* connections_[DRUM_VOICES * 3 + 4];  // Each voice: player->fade->left->right, plus finals
  uint8_t numConnections_;

  // Sample data mapping (MIDI note -> PROGMEM array pointer in AudioPlayMemory format)
  const unsigned int* sampleMap_[128];  // Map for all MIDI notes

  // Helper functions
  int allocateVoice();
  void initializeSampleMap();
  void getPanGains(uint8_t midiNote, float& leftGain, float& rightGain, float& panPosition);
  void chokeGroup(uint8_t midiNote);

  // State
  bool enabled_;
  bool initialized_;
  uint32_t droppedNotes_;
};

#endif // DRUM_SAMPLER_V2_H
