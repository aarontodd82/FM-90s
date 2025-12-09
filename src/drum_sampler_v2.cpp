#include "drum_sampler_v2.h"

// Include all drum sample headers (PROGMEM data)
#include "drums/acoustic_bass_drum_35.h"
#include "drums/acoustic_snare_38.h"
#include "drums/bass_drum_1_36.h"
#include "drums/bell_tree_84.h"
#include "drums/cabasa_69.h"
#include "drums/castanets_85.h"
#include "drums/chinese_cymbal_52.h"
#include "drums/claves_75.h"
#include "drums/closed_hi_hat_42.h"
#include "drums/cowbell_56.h"
#include "drums/crash_cymbal_1_49.h"
#include "drums/crash_cymbal_2_57.h"
#include "drums/electric_snare_40.h"
#include "drums/hand_clap_39.h"
#include "drums/hi_bongo_60.h"
#include "drums/hi_mid_tom_48.h"
#include "drums/hi_wood_block_76.h"
#include "drums/high_agogo_67.h"
#include "drums/high_floor_tom_43.h"
#include "drums/high_q_27.h"
#include "drums/high_timbale_65.h"
#include "drums/high_tom_50.h"
#include "drums/jingle_bell_83.h"
#include "drums/long_guiro_74.h"
#include "drums/long_whistle_72.h"
#include "drums/low_agogo_68.h"
#include "drums/low_bongo_61.h"
#include "drums/low_conga_64.h"
#include "drums/low_floor_tom_41.h"
#include "drums/low_mid_tom_47.h"
#include "drums/low_timbale_66.h"
#include "drums/low_tom_45.h"
#include "drums/low_wood_block_77.h"
#include "drums/maracas_70.h"
#include "drums/metronome_bell_34.h"
#include "drums/metronome_click_33.h"
#include "drums/mute_cuica_78.h"
#include "drums/mute_hi_conga_62.h"
#include "drums/mute_surdo_86.h"
#include "drums/mute_triangle_80.h"
#include "drums/open_cuica_79.h"
#include "drums/open_hi_conga_63.h"
#include "drums/open_hi_hat_46.h"
#include "drums/open_surdo_87.h"
#include "drums/open_triangle_81.h"
#include "drums/pedal_hi_hat_44.h"
#include "drums/ride_bell_53.h"
#include "drums/ride_cymbal_1_51.h"
#include "drums/ride_cymbal_2_59.h"
#include "drums/scratch_pull_30.h"
#include "drums/scratch_push_29.h"
#include "drums/shaker_82.h"
#include "drums/short_guiro_73.h"
#include "drums/short_whistle_71.h"
#include "drums/side_stick_37.h"
#include "drums/slap_28.h"
#include "drums/splash_cymbal_55.h"
#include "drums/square_click_32.h"
#include "drums/sticks_31.h"
#include "drums/tambourine_54.h"
#include "drums/vibraslap_58.h"

DrumSamplerV2::DrumSamplerV2()
  : enabled_(true)
  , initialized_(false)
  , droppedNotes_(0)
  , numConnections_(0)
{
  // Initialize voice array
  for (int i = 0; i < DRUM_VOICES; i++) {
    voices_[i].player = nullptr;
    voices_[i].fade = nullptr;
    voices_[i].midiNote = 0;
    voices_[i].startTime = 0;
    voices_[i].active = false;
  }

  // Initialize connections
  for (int i = 0; i < DRUM_VOICES + 2; i++) {
    connections_[i] = nullptr;
  }

  // Initialize sample map (all nullptr initially)
  for (int i = 0; i < 128; i++) {
    sampleMap_[i] = nullptr;
  }
}

DrumSamplerV2::~DrumSamplerV2() {
  for (int i = 0; i < DRUM_VOICES; i++) {
    if (voices_[i].player) {
      delete voices_[i].player;
    }
    if (voices_[i].fade) {
      delete voices_[i].fade;
    }
  }

  for (int i = 0; i < numConnections_; i++) {
    if (connections_[i]) {
      delete connections_[i];
    }
  }
}

void DrumSamplerV2::initializeSampleMap() {
  // Map each MIDI note to its PROGMEM sample data (AudioPlayMemory format)
  // GM Drum Map: notes 27-87

  sampleMap_[27] = high_q_27_data;
  sampleMap_[28] = slap_28_data;

  sampleMap_[29] = scratch_push_29_data;
  sampleMap_[30] = scratch_pull_30_data;
  sampleMap_[31] = sticks_31_data;
  sampleMap_[32] = square_click_32_data;
  sampleMap_[33] = metronome_click_33_data;
  sampleMap_[34] = metronome_bell_34_data;
  sampleMap_[35] = acoustic_bass_drum_35_data;
  sampleMap_[36] = bass_drum_1_36_data;
  sampleMap_[37] = side_stick_37_data;
  sampleMap_[38] = acoustic_snare_38_data;
  sampleMap_[39] = hand_clap_39_data;
  sampleMap_[40] = electric_snare_40_data;
  sampleMap_[41] = low_floor_tom_41_data;
  sampleMap_[42] = closed_hi_hat_42_data;
  sampleMap_[43] = high_floor_tom_43_data;
  sampleMap_[44] = pedal_hi_hat_44_data;
  sampleMap_[45] = low_tom_45_data;
  sampleMap_[46] = open_hi_hat_46_data;
  sampleMap_[47] = low_mid_tom_47_data;
  sampleMap_[48] = hi_mid_tom_48_data;
  sampleMap_[49] = crash_cymbal_1_49_data;
  sampleMap_[50] = high_tom_50_data;
  sampleMap_[51] = ride_cymbal_1_51_data;
  sampleMap_[52] = chinese_cymbal_52_data;
  sampleMap_[53] = ride_bell_53_data;
  sampleMap_[54] = tambourine_54_data;
  sampleMap_[55] = splash_cymbal_55_data;
  sampleMap_[56] = cowbell_56_data;
  sampleMap_[57] = crash_cymbal_2_57_data;
  sampleMap_[58] = vibraslap_58_data;
  sampleMap_[59] = ride_cymbal_2_59_data;
  sampleMap_[60] = hi_bongo_60_data;
  sampleMap_[61] = low_bongo_61_data;
  sampleMap_[62] = mute_hi_conga_62_data;
  sampleMap_[63] = open_hi_conga_63_data;
  sampleMap_[64] = low_conga_64_data;
  sampleMap_[65] = high_timbale_65_data;
  sampleMap_[66] = low_timbale_66_data;
  sampleMap_[67] = high_agogo_67_data;
  sampleMap_[68] = low_agogo_68_data;
  sampleMap_[69] = cabasa_69_data;
  sampleMap_[70] = maracas_70_data;
  sampleMap_[71] = short_whistle_71_data;
  sampleMap_[72] = long_whistle_72_data;
  sampleMap_[73] = short_guiro_73_data;
  sampleMap_[74] = long_guiro_74_data;
  sampleMap_[75] = claves_75_data;
  sampleMap_[76] = hi_wood_block_76_data;
  sampleMap_[77] = low_wood_block_77_data;
  sampleMap_[78] = mute_cuica_78_data;
  sampleMap_[79] = open_cuica_79_data;
  sampleMap_[80] = mute_triangle_80_data;
  sampleMap_[81] = open_triangle_81_data;
  sampleMap_[82] = shaker_82_data;
  sampleMap_[83] = jingle_bell_83_data;
  sampleMap_[84] = bell_tree_84_data;
  sampleMap_[85] = castanets_85_data;
  sampleMap_[86] = mute_surdo_86_data;
  sampleMap_[87] = open_surdo_87_data;
}

void DrumSamplerV2::getPanGains(uint8_t midiNote, float& leftGain, float& rightGain, float& panPosition) {
  // Hard-coded stereo pan positions for each GM drum
  // Pan range: -1.0 (full left) to +1.0 (full right), 0.0 = center
  // Using constant-power panning for smooth stereo imaging

  static const float panMap[128] = {
    // 0-26: Not used
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,

    // GM Drum Map (27-87)
    0.0f,   // 27: High Q (center)
    -0.2f,  // 28: Slap (slight left)
    -0.3f,  // 29: Scratch Push (left)
    0.3f,   // 30: Scratch Pull (right)
    -0.7f,  // 31: Sticks (hard left)
    0.0f,   // 32: Square Click (center)
    0.0f,   // 33: Metronome Click (center)
    0.0f,   // 34: Metronome Bell (center)
    0.0f,   // 35: Acoustic Bass Drum (center)
    0.0f,   // 36: Bass Drum 1 (center)
    -0.4f,  // 37: Side Stick (left)
    -0.1f,  // 38: Acoustic Snare (slight left)
    0.0f,   // 39: Hand Clap (center)
    0.0f,   // 40: Electric Snare (center)
    -0.5f,  // 41: Low Floor Tom (left)
    0.3f,   // 42: Closed Hi-Hat (slight right)
    -0.3f,  // 43: High Floor Tom (slight left)
    0.3f,   // 44: Pedal Hi-Hat (slight right)
    -0.5f,  // 45: Low Tom (left)
    0.4f,   // 46: Open Hi-Hat (right)
    -0.3f,  // 47: Low-Mid Tom (slight left)
    -0.1f,  // 48: Hi-Mid Tom (slight left)
    -0.8f,  // 49: Crash Cymbal 1 (hard left)
    0.2f,   // 50: High Tom (slight right)
    0.6f,   // 51: Ride Cymbal 1 (right)
    -0.9f,  // 52: Chinese Cymbal (far left)
    0.7f,   // 53: Ride Bell (right)
    0.5f,   // 54: Tambourine (right)
    -0.7f,  // 55: Splash Cymbal (hard left)
    0.1f,   // 56: Cowbell (slight right)
    0.8f,   // 57: Crash Cymbal 2 (hard right)
    0.6f,   // 58: Vibraslap (right)
    0.6f,   // 59: Ride Cymbal 2 (right)

    // Auxiliary percussion (60-87) - spread wide for separation
    -0.6f,  // 60: Hi Bongo
    -0.8f,  // 61: Low Bongo
    0.7f,   // 62: Mute Hi Conga
    0.5f,   // 63: Open Hi Conga
    0.3f,   // 64: Low Conga
    -0.5f,  // 65: High Timbale
    -0.7f,  // 66: Low Timbale
    0.8f,   // 67: High Agogo
    0.6f,   // 68: Low Agogo
    -0.4f,  // 69: Cabasa
    0.4f,   // 70: Maracas
    0.7f,   // 71: Short Whistle
    0.9f,   // 72: Long Whistle
    -0.6f,  // 73: Short Guiro
    -0.8f,  // 74: Long Guiro
    0.0f,   // 75: Claves (center)
    0.5f,   // 76: Hi Wood Block
    0.3f,   // 77: Low Wood Block
    -0.7f,  // 78: Mute Cuica
    -0.9f,  // 79: Open Cuica
    0.6f,   // 80: Mute Triangle
    0.8f,   // 81: Open Triangle
    -0.5f,  // 82: Shaker
    0.7f,   // 83: Jingle Bell
    0.9f,   // 84: Bell Tree
    -0.8f,  // 85: Castanets
    -0.4f,  // 86: Mute Surdo
    -0.6f,  // 87: Open Surdo

    // 88-127: Not used
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };

  panPosition = panMap[midiNote];

  // Constant-power panning for smooth stereo imaging
  // Left and right gains sum to maintain constant perceived loudness
  leftGain = sqrt((1.0f - panPosition) / 2.0f);
  rightGain = sqrt((1.0f + panPosition) / 2.0f);
}

void DrumSamplerV2::chokeGroup(uint8_t midiNote) {
  // Define choke groups - notes that should silence each other
  // When one note in a group plays, it stops all others in the group

  // Hi-hat choke group: closed, pedal, and open hi-hats
  static const uint8_t hihatGroup[] = {42, 44, 46};  // Closed, Pedal, Open
  static const int hihatGroupSize = 3;

  // Check if this note is in the hi-hat group
  bool isHihat = false;
  for (int i = 0; i < hihatGroupSize; i++) {
    if (midiNote == hihatGroup[i]) {
      isHihat = true;
      break;
    }
  }

  if (isHihat) {
    // Stop all other hi-hat sounds
    for (int v = 0; v < DRUM_VOICES; v++) {
      if (voices_[v].active) {
        // Check if this voice is playing a hi-hat sound
        for (int i = 0; i < hihatGroupSize; i++) {
          if (voices_[v].midiNote == hihatGroup[i] && voices_[v].midiNote != midiNote) {
            // This voice is playing a different hi-hat sound - choke it
            voices_[v].fade->fadeOut(5);  // Quick 5ms fade out
            voices_[v].active = false;

            // Zero the mixer gains immediately
            if (v < 4) {
              leftMixer1_.gain(v, 0.0f);
              rightMixer1_.gain(v, 0.0f);
            } else {
              leftMixer2_.gain(v - 4, 0.0f);
              rightMixer2_.gain(v - 4, 0.0f);
            }
          }
        }
      }
    }
  }

  // Could add more choke groups here in the future:
  // - Conga groups (mute/open)
  // - Triangle groups (mute/open)
  // - Cuica groups (mute/open)
}

bool DrumSamplerV2::begin() {
  if (initialized_) {
    return true;
  }

  // // Serial.println("\n=== Initializing DrumSamplerV2 (AudioPlayMemory + PROGMEM) ===");

  // Initialize sample map
  initializeSampleMap();

  // Create AudioPlayMemory players and AudioEffectFade
  for (int i = 0; i < DRUM_VOICES; i++) {
    voices_[i].player = new AudioPlayMemory();
    if (!voices_[i].player) {
      // // Serial.printf("Failed to allocate player for voice %d\n", i);
      return false;
    }

    voices_[i].fade = new AudioEffectFade();
    if (!voices_[i].fade) {
      // // Serial.printf("Failed to allocate fade for voice %d\n", i);
      return false;
    }

    voices_[i].active = false;
    voices_[i].midiNote = 0;
    voices_[i].startTime = 0;
  }

  // Set up stereo audio routing: player -> fade -> left/right mixers
  int connIdx = 0;

  // First 4 voices (0-3)
  for (int i = 0; i < 4 && i < DRUM_VOICES; i++) {
    // player -> fade
    connections_[connIdx++] = new AudioConnection(*voices_[i].player, 0, *voices_[i].fade, 0);
    // fade -> left mixer
    connections_[connIdx++] = new AudioConnection(*voices_[i].fade, 0, leftMixer1_, i);
    // fade -> right mixer
    connections_[connIdx++] = new AudioConnection(*voices_[i].fade, 0, rightMixer1_, i);
  }

  // Next 4 voices (4-7)
  for (int i = 4; i < DRUM_VOICES && i < 8; i++) {
    // player -> fade
    connections_[connIdx++] = new AudioConnection(*voices_[i].player, 0, *voices_[i].fade, 0);
    // fade -> left mixer
    connections_[connIdx++] = new AudioConnection(*voices_[i].fade, 0, leftMixer2_, i - 4);
    // fade -> right mixer
    connections_[connIdx++] = new AudioConnection(*voices_[i].fade, 0, rightMixer2_, i - 4);
  }

  // Connect intermediate mixers to final outputs
  connections_[connIdx++] = new AudioConnection(leftMixer1_, 0, leftFinal_, 0);
  connections_[connIdx++] = new AudioConnection(leftMixer2_, 0, leftFinal_, 1);
  connections_[connIdx++] = new AudioConnection(rightMixer1_, 0, rightFinal_, 0);
  connections_[connIdx++] = new AudioConnection(rightMixer2_, 0, rightFinal_, 1);

  numConnections_ = connIdx;

  // Initialize all mixer gains to 0 (will be set per-note based on pan + velocity)
  for (int i = 0; i < 4; i++) {
    leftMixer1_.gain(i, 0.0);
    leftMixer2_.gain(i, 0.0);
    rightMixer1_.gain(i, 0.0);
    rightMixer2_.gain(i, 0.0);
  }

  // Final mixer combines intermediate mixers
  leftFinal_.gain(0, 0.5);
  leftFinal_.gain(1, 0.5);
  rightFinal_.gain(0, 0.5);
  rightFinal_.gain(1, 0.5);

  // Count available samples
  int sampleCount = 0;
  for (int i = 27; i <= 87; i++) {
    if (sampleMap_[i] != nullptr) {
      sampleCount++;
    }
  }

  // // Serial.printf("Loaded %d drum samples (GM notes 27-87)\n", sampleCount);
  // // Serial.printf("Voice polyphony: %d\n", DRUM_VOICES);

  initialized_ = true;
  // // Serial.println("DrumSamplerV2 initialized successfully");

  return true;
}

int DrumSamplerV2::allocateVoice() {
  // First, try to find a free voice
  for (int i = 0; i < DRUM_VOICES; i++) {
    if (!voices_[i].active || !voices_[i].player->isPlaying()) {
      voices_[i].active = false;  // Mark as available
      return i;
    }
  }

  // All voices busy - steal the oldest one
  int oldestIdx = 0;
  uint32_t oldestTime = voices_[0].startTime;

  for (int i = 1; i < DRUM_VOICES; i++) {
    if (voices_[i].startTime < oldestTime) {
      oldestTime = voices_[i].startTime;
      oldestIdx = i;
    }
  }

  droppedNotes_++;
  return oldestIdx;
}

void DrumSamplerV2::noteOn(uint8_t midiNote, uint8_t velocity) {
  if (!enabled_ || !initialized_) {
    return;
  }

  // Check if we have a sample for this note
  if (sampleMap_[midiNote] == nullptr) {
    return;  // No sample for this note
  }

  // Handle choke groups (e.g., open hi-hat stops closed hi-hat)
  chokeGroup(midiNote);

  // Allocate a voice
  int voiceIdx = allocateVoice();
  if (voiceIdx < 0) {
    return;  // Shouldn't happen, but safety check
  }

  // Calculate logarithmic velocity scaling (0.0 to 1.0)
  float velocityScale = (velocity > 0) ? (log(velocity) / log(127.0f)) : 0.0f;
  velocityScale = velocityScale * velocityScale;  // Square it for more dynamic range

  // Get stereo pan gains for this drum
  float leftGain, rightGain, panPosition;
  getPanGains(midiNote, leftGain, rightGain, panPosition);

  // Calculate pan-dependent boost to compensate for perceived loudness
  // Center sounds need more boost (1.4x), hard-panned sounds need less (1.0x)
  // This is because center sounds come from both speakers and seem quieter perceptually
  float panBoost = 1.0f + 0.4f * (1.0f - fabs(panPosition));

  // Combine velocity, pan, and pan-dependent boost
  float finalLeftGain = panBoost * velocityScale * leftGain;
  float finalRightGain = panBoost * velocityScale * rightGain;

  // Apply gains to the appropriate mixer channels (stereo)
  if (voiceIdx < 4) {
    leftMixer1_.gain(voiceIdx, finalLeftGain);
    rightMixer1_.gain(voiceIdx, finalRightGain);
  } else {
    leftMixer2_.gain(voiceIdx - 4, finalLeftGain);
    rightMixer2_.gain(voiceIdx - 4, finalRightGain);
  }

  // Play the sample
  // AudioPlayMemory::play() expects unsigned int* in its special format
  voices_[voiceIdx].player->play(sampleMap_[midiNote]);

  // Fade in immediately (1ms) to prevent clicks from mixer discontinuities
  voices_[voiceIdx].fade->fadeIn(1);

  // Get sample length and schedule fadeOut to happen 20ms before the end
  uint32_t sampleLength = voices_[voiceIdx].player->lengthMillis();
  uint32_t fadeOutDelay = (sampleLength > 20) ? (sampleLength - 20) : 0;

  voices_[voiceIdx].active = true;
  voices_[voiceIdx].midiNote = midiNote;
  voices_[voiceIdx].startTime = millis();
  voices_[voiceIdx].fadeOutTime = millis() + fadeOutDelay;
}

void DrumSamplerV2::noteOff(uint8_t midiNote) {
  // For drums, we let them play to completion
  // NoteOff is ignored (drum samples are one-shots)
}

void DrumSamplerV2::update() {
  if (!initialized_) {
    return;
  }

  // Check for voices that need fadeOut triggered
  uint32_t now = millis();
  for (int i = 0; i < DRUM_VOICES; i++) {
    if (voices_[i].active) {
      // Check if it's time to trigger fadeOut (20ms before sample ends)
      if (voices_[i].fadeOutTime > 0 && now >= voices_[i].fadeOutTime) {
        voices_[i].fade->fadeOut(20);  // 20ms fadeout
        voices_[i].fadeOutTime = 0;  // Mark as triggered
      }

      // Check if sample finished
      if (!voices_[i].player->isPlaying()) {
        voices_[i].active = false;
      }
    }
  }
}

void DrumSamplerV2::printStatistics() {
  // Count active voices
  int active = 0;
  for (int i = 0; i < DRUM_VOICES; i++) {
    if (voices_[i].active && voices_[i].player->isPlaying()) {
      active++;
    }
  }

  // Serial.printf("DrumSamplerV2: voices=%d/%d, dropped=%lu\n",
  //              active, DRUM_VOICES, droppedNotes_);
}
