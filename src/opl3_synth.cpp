#include "opl3_synth.h"
#include "instruments_wrapper.h"
#include "debug_config.h"  // For DEBUG_SERIAL_ENABLED
#include "audio_system.h"  // For volume control during reset
#include "audio_globals.h"  // For audioShield access
#include <math.h>

// Note F-numbers for one octave (C through B) plus 2 semitones on each side for pitch bend
// These are the base F-numbers for octave 4 that get shifted for other octaves
static const uint16_t noteFNumbers[16] = {
  0x132, 0x144,  // A#, B (for pitch bend down from C)
  0x156, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,  // C, C#, D, D#, E, F
  0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287,  // F#, G, G#, A, A#, B
  0x2AC, 0x2D6   // C, C# (for pitch bend up from B)
};

// Check if a GM program should be prioritized for 4-op allocation
// These instruments benefit most from 4-op synthesis
bool OPL3Synth::prefer4opForProgram(uint8_t program) {
  // Pianos (0-7) - complex harmonics, long sustain
  if (program <= 7) return true;

  // Organs (16-20)
  if (program >= 16 && program <= 20) return true;

  // Bass (32-39) - including synth bass
  if (program >= 32 && program <= 39) return true;

  // String Ensembles (48-51)
  if (program >= 48 && program <= 51) return true;

  // Brass (56-63) - including synth brass
  if (program >= 56 && program <= 63) return true;

  // Synth Leads (80-87)
  if (program >= 80 && program <= 87) return true;

  // Synth Pads (88-95)
  if (program >= 88 && program <= 95) return true;

  return false;
}

void OPL3Synth::begin(const OPL3Pins& pins) {
  if (opl) delete opl;
  opl = new OPL3DuoLogged(pins.addrA2, pins.addrA1, pins.addrA0, pins.latchWR, pins.resetIC);
  opl->begin();
  opl->setOPL3Enabled(true);
  // Don't enable all 4-op - we'll enable dynamically

  for (auto &v : voices_) v = Voice{};
  for (auto &c : ch_) {
    c.program = 0;
    c.volume = 1.0f; // Full volume by default
    c.pan = 64;
    c.sustain = false;
    c.pitchBend = 0;
    c.pbRange = 2;
  }

  // Center panning for all channels
  for (uint8_t ch=0; ch<36; ++ch) {
    opl->setPanning(ch, true, true);
  }
}

void OPL3Synth::resetAll() {
  allNotesOff();
  for (auto &c : ch_) {
    c.program = 0;
    c.volume = 1.0f;
    c.pan = 64;
    c.sustain = false;
    c.pitchBend = 0;
    c.pbRange = 2;
  }
}

void OPL3Synth::allNotesOff() {
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type != VOICE_FREE) {
      freeVoice(i);
    }
  }
}

void OPL3Synth::hardwareReset() {
  if (!opl) return;

  // Note: Audio is already muted by the caller (player's stop() method)
  // No need to mute/unmute here

  // Use the library's proven reset() method instead of our custom implementation
  // The ArduinoOPL2 library's reset() properly:
  // 1. Does hardware reset (toggles reset pin)
  // 2. Initializes chip registers
  // 3. Clears all channel KEY-ON bits (B0 registers)
  // 4. Sets all operator volumes to silent (0x40 registers = 0x3F)
  opl->reset();

  // Clear our voice tracking state
  allNotesOff();

  // Reset center panning for all channels
  for (uint8_t ch = 0; ch < 36; ch++) {
    opl->setPanning(ch, true, true);
  }
}

uint8_t OPL3Synth::count4opVoices() {
  uint8_t count = 0;
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type == VOICE_4OP) count++;
  }
  return count;
}

int OPL3Synth::allocateVoice(uint8_t midiCh, uint8_t key, bool want4op) {
  // Reserved slots approach: First 6 slots open to all, last 6 reserved for high-priority
  const int RESERVED_SLOTS = 6;
  uint8_t current4op = count4opVoices();
  bool allow4op = false;

  if (want4op && !force2OpOnly_) {
    if (prefer4opForProgram(ch_[midiCh].program)) {
      // High-priority instruments can use all 12 slots
      allow4op = (current4op < max4OpVoices_);
    } else {
      // Low-priority instruments can only use first 6 slots
      allow4op = (current4op < (max4OpVoices_ - RESERVED_SLOTS));
    }
  }

  // Find free voice slot
  int freeSlot = -1;
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type == VOICE_FREE) {
      freeSlot = i;
      break;
    }
  }

  if (freeSlot < 0) {
    // Steal oldest voice (any type)
    uint32_t oldest = 0xFFFFFFFF;
    for (int i=0; i<MAX_VOICES; ++i) {
      if (voices_[i].startTick < oldest) {
        oldest = voices_[i].startTick;
        freeSlot = i;
      }
    }
    if (freeSlot >= 0) {
      freeVoice(freeSlot);
    }
  }

  if (freeSlot < 0) return -1;

  // Allocate OPL channel
  if (allow4op) {
    // Find free 4-op channel (0-11)
    for (uint8_t ch4op = 0; ch4op < 12; ++ch4op) {
      bool inUse = false;

      // Check if this 4-op channel index is used by another 4-op voice
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type == VOICE_4OP && voices_[i].oplChannel == ch4op) {
          inUse = true;
          break;
        }
      }

      // Also check if the underlying physical channels are used by 2-op voices
      // This prevents conflicts where a 2-op voice is playing on a channel
      // that would be taken over by the 4-op pair
      if (!inUse) {
        uint8_t physCh0 = opl->get4OPControlChannel(ch4op, 0);
        uint8_t physCh1 = opl->get4OPControlChannel(ch4op, 1);
        for (int i=0; i<MAX_VOICES; ++i) {
          if (voices_[i].type == VOICE_2OP &&
              (voices_[i].oplChannel == physCh0 || voices_[i].oplChannel == physCh1)) {
            inUse = true;
            break;
          }
        }
      }

      if (!inUse) {
        voices_[freeSlot].type = VOICE_4OP;
        voices_[freeSlot].oplChannel = ch4op;
        opl->set4OPChannelEnabled(ch4op, true);

        // Debug: Show 4-op allocation
        // Commented out - too verbose during normal playback
        // Serial.print("4-op ALLOCATED: Ch");
        // Serial.print(midiCh);
        // Serial.print(" Note");
        // Serial.print(key);
        // Serial.print(" (OPL 4-op channel ");
        // Serial.print(ch4op);
        // Serial.print(", total 4-op: ");
        // Serial.print(current4op + 1);
        // Serial.print("/");
        // Serial.print(max4OpVoices_);
        // Serial.println(")");

        return freeSlot;
      }
    }
  }

  // Allocate 2-op (physical channel 0-35, avoiding drum channels and 4-op pairs in use)
  for (uint8_t phys = 0; phys < 36; ++phys) {
    // Skip drum channels ONLY if drum sampler is disabled (using FM drums)
    // When drum sampler is enabled, these 6 channels become available for melodic use
    if (!drumSamplerEnabled_) {
      bool isDrum = false;
      for (uint8_t d=0; d<NUM_DRUM_CHANNELS; ++d) {
        if (phys == drumChannels_[d]) { isDrum = true; break; }
      }
      if (isDrum) continue;
    }

    // Check if this physical channel is part of an active 4-op pair
    bool in4op = false;
    for (int i=0; i<MAX_VOICES; ++i) {
      if (voices_[i].type == VOICE_4OP) {
        uint8_t ctrl = opl->get4OPControlChannel(voices_[i].oplChannel, 0);
        uint8_t pair = opl->get4OPControlChannel(voices_[i].oplChannel, 1);
        if (phys == ctrl || phys == pair) {
          in4op = true;
          break;
        }
      }
    }
    if (in4op) continue;

    // Check if already in use as 2-op
    bool inUse = false;
    for (int i=0; i<MAX_VOICES; ++i) {
      if (voices_[i].type == VOICE_2OP && voices_[i].oplChannel == phys) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      voices_[freeSlot].type = VOICE_2OP;
      voices_[freeSlot].oplChannel = phys;
      return freeSlot;
    }
  }

  return -1;
}

void OPL3Synth::freeVoice(int vid) {
  Voice &v = voices_[vid];
  if (v.type == VOICE_FREE) return;

  if (v.type == VOICE_4OP) {
    uint8_t physCh = opl->get4OPControlChannel(v.oplChannel, 0);
    opl->setKeyOn(physCh, false);
    opl->set4OPChannelEnabled(v.oplChannel, false);

    // Debug: Show 4-op freed
    // Commented out - too verbose during normal playback
    // Serial.print("4-op FREED: Ch");
    // Serial.print(v.midiCh);
    // Serial.print(" Note");
    // Serial.print(v.midiKey);
    // Serial.print(" (OPL 4-op channel ");
    // Serial.print(v.oplChannel);
    // Serial.print(", total 4-op now: ");
    // Serial.print(count4opVoices() - 1);
    // Serial.print("/");
    // Serial.print(max4OpVoices_);
    // Serial.println(")");
  } else if (v.type == VOICE_2OP) {
    opl->setKeyOn(v.oplChannel, false);
  }

  v = Voice{};
}

int OPL3Synth::allocateDrumChannel() {
  // First, try to find a free drum channel + free voice slot
  for (uint8_t d=0; d<NUM_DRUM_CHANNELS; ++d) {
    uint8_t phys = drumChannels_[d];
    bool inUse = false;
    for (int i=0; i<MAX_VOICES; ++i) {
      if (voices_[i].type == VOICE_2OP && voices_[i].oplChannel == phys) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      // Find free voice slot for drum
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type == VOICE_FREE) {
          voices_[i].type = VOICE_2OP;
          voices_[i].oplChannel = phys;
          return i;
        }
      }
    }
  }

  // No free drum channel+slot combo found - try to steal oldest drum voice
  int oldestDrumVoice = -1;
  uint32_t oldestTick = 0xFFFFFFFF;

  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type == VOICE_2OP && voices_[i].midiCh == 9) {  // Channel 9 = drums
      if (voices_[i].startTick < oldestTick) {
        oldestTick = voices_[i].startTick;
        oldestDrumVoice = i;
      }
    }
  }

  if (oldestDrumVoice >= 0) {
    // Steal the oldest drum voice, reuse its drum channel
    uint8_t drumCh = voices_[oldestDrumVoice].oplChannel;
    freeVoice(oldestDrumVoice);
    voices_[oldestDrumVoice].type = VOICE_2OP;
    voices_[oldestDrumVoice].oplChannel = drumCh;
    return oldestDrumVoice;
  }

  // Still no luck - find any free voice slot and steal a drum channel
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type == VOICE_FREE) {
      // Steal oldest drum's physical channel
      int oldestDrum = -1;
      uint32_t oldest = 0xFFFFFFFF;
      for (int j=0; j<MAX_VOICES; ++j) {
        if (voices_[j].type == VOICE_2OP && voices_[j].midiCh == 9) {
          if (voices_[j].startTick < oldest) {
            oldest = voices_[j].startTick;
            oldestDrum = j;
          }
        }
      }
      if (oldestDrum >= 0) {
        uint8_t drumCh = voices_[oldestDrum].oplChannel;
        freeVoice(oldestDrum);
        voices_[i].type = VOICE_2OP;
        voices_[i].oplChannel = drumCh;
        return i;
      }
    }
  }

  return -1;
}

uint8_t OPL3Synth::applyDrumInstrument(uint8_t physCh, uint8_t noteNum, uint8_t velocity) {
  const uint8_t DRUM_NOTE_BASE = 28;
  const uint8_t NUM_MIDI_DRUMS = 60;

  Instrument drumInst = opl->createInstrument();
  uint8_t transpose = 60; // Default middle C if no drum found

  if (noteNum >= DRUM_NOTE_BASE && noteNum < (DRUM_NOTE_BASE + NUM_MIDI_DRUMS)) {
    uint8_t drumIndex = noteNum - DRUM_NOTE_BASE;
    if (Drums::midiDrums[drumIndex] != nullptr) {
      drumInst = opl->loadInstrument(Drums::midiDrums[drumIndex]);
      transpose = drumInst.transpose; // Get the transpose value from the instrument
    }
  }

  // Apply velocity to drums (logarithmic like melodic)
  float drumVel = log(max(1.0f, (float)velocity)) / log(127.0f);
  opl->setInstrument(physCh, drumInst, drumVel);

  return transpose; // Return the transpose value to be stored in the voice
}

void OPL3Synth::applyInstrument(Voice& v, uint8_t midiCh) {
  uint8_t program = min(ch_[midiCh].program, (uint8_t)127);

  if (v.type == VOICE_4OP) {
    // Load and store 4-op instrument in memory
    ch_[midiCh].instrument4op = opl->loadInstrument4OP(Instruments4OP::midiInstruments[program]);
    opl->setInstrument4OP(v.oplChannel, ch_[midiCh].instrument4op, 0.0f);
  } else {
    // Load and store 2-op instrument in memory
    ch_[midiCh].instrument2op = opl->loadInstrument(Instruments2OP::midiInstruments[program]);
    opl->setInstrument(v.oplChannel, ch_[midiCh].instrument2op, 0.0f);
  }
}

void OPL3Synth::applyVolume(Voice& v, uint8_t midiCh) {
  // Logarithmic volume calculation like the example
  float noteVel = log(max(1.0f, (float)v.velocity)) / log(127.0f);
  float volume = noteVel * ch_[midiCh].volume;

  if (v.type == VOICE_4OP) {
    // For 4-op: scale each operator proportionally to preserve timbre
    // Read from stored instrument in memory, not from chip
    Instrument4OP& inst = ch_[midiCh].instrument4op;
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t physCh = opl->get4OPControlChannel(v.oplChannel, i);

      // Read original operator levels from stored instrument and scale proportionally
      float op1Level = (float)(63 - inst.subInstrument[i].operators[OPERATOR1].outputLevel) / 63.0f;
      float op2Level = (float)(63 - inst.subInstrument[i].operators[OPERATOR2].outputLevel) / 63.0f;

      uint8_t volumeOp1 = (uint8_t)(op1Level * volume * 63.0f);
      uint8_t volumeOp2 = (uint8_t)(op2Level * volume * 63.0f);

      opl->setVolume(physCh, OPERATOR1, 63 - volumeOp1);
      opl->setVolume(physCh, OPERATOR2, 63 - volumeOp2);
    }
  } else {
    // For 2-op: scale each operator proportionally to preserve timbre
    // Read from stored instrument in memory, not from chip
    Instrument& inst = ch_[midiCh].instrument2op;

    float op1Level = (float)(63 - inst.operators[OPERATOR1].outputLevel) / 63.0f;
    float op2Level = (float)(63 - inst.operators[OPERATOR2].outputLevel) / 63.0f;

    uint8_t volumeOp1 = (uint8_t)(op1Level * volume * 63.0f);
    uint8_t volumeOp2 = (uint8_t)(op2Level * volume * 63.0f);

    opl->setVolume(v.oplChannel, OPERATOR1, 63 - volumeOp1);
    opl->setVolume(v.oplChannel, OPERATOR2, 63 - volumeOp2);
  }
}

void OPL3Synth::applyPitch(Voice& v, uint8_t midiCh, int16_t bend) {
  uint8_t note = max(24, min(v.midiKey, (uint8_t)119));
  uint8_t octave = 1 + (note - 24) / 12;
  uint8_t noteInOctave = note % 12;

  // Get the control channel (for 4-op) or the channel itself (for 2-op)
  uint8_t controlCh = (v.type == VOICE_4OP) ?
    opl->get4OPControlChannel(v.oplChannel, 0) : v.oplChannel;

  // If no pitch bend, just play the note normally
  if (bend == 0) {
    opl->playNote(controlCh, octave, noteInOctave);
    return;
  }

  // Calculate pitch bend amount (bend range is typically ±2 semitones)
  // bend is in range -8192 to +8191
  float bendAmount = (float)bend / 8192.0f;  // Now in range -1.0 to +1.0
  bendAmount *= ch_[midiCh].pbRange;  // Apply pitch bend range (default 2 semitones)

  // Calculate the target F-number based on pitch bend
  uint16_t targetFNum;

  if (bendAmount < 0) {
    // Bending down - interpolate towards lower note
    float absBend = -bendAmount;  // Make positive for calculation
    int semitonesBend = (int)absBend;
    float fractionalBend = absBend - semitonesBend;

    // Get F-numbers for interpolation
    int lowerIdx = noteInOctave + 2 - semitonesBend;
    if (lowerIdx < 0) lowerIdx = 0;  // Clamp to array bounds

    uint16_t lowerFNum = noteFNumbers[lowerIdx];
    uint16_t nextFNum = (lowerIdx > 0) ? noteFNumbers[lowerIdx - 1] : lowerFNum;

    // Interpolate between the two F-numbers
    float fDelta = (float)(lowerFNum - nextFNum) * fractionalBend;
    targetFNum = lowerFNum - (uint16_t)fDelta;
  } else {
    // Bending up - interpolate towards higher note
    int semitonesBend = (int)bendAmount;
    float fractionalBend = bendAmount - semitonesBend;

    // Get F-numbers for interpolation
    int upperIdx = noteInOctave + 2 + semitonesBend;
    if (upperIdx > 15) upperIdx = 15;  // Clamp to array bounds

    uint16_t upperFNum = noteFNumbers[upperIdx];
    uint16_t nextFNum = (upperIdx < 15) ? noteFNumbers[upperIdx + 1] : upperFNum;

    // Interpolate between the two F-numbers
    float fDelta = (float)(nextFNum - upperFNum) * fractionalBend;
    targetFNum = upperFNum + (uint16_t)fDelta;
  }

  // Set the F-number and block (octave) directly
  opl->setFNumber(controlCh, targetFNum);
  opl->setBlock(controlCh, octave);
  opl->setKeyOn(controlCh, true);
}

void OPL3Synth::applyPanning(Voice& v, uint8_t midiCh) {
  uint8_t panValue = ch_[midiCh].pan;

  // Map MIDI pan (0-127) to OPL3's 3 positions:
  // 0-42: Hard left
  // 43-84: Center (both channels)
  // 85-127: Hard right
  bool leftOn, rightOn;

  if (panValue < 43) {
    // Hard left
    leftOn = true;
    rightOn = false;
  } else if (panValue < 85) {
    // Center
    leftOn = true;
    rightOn = true;
  } else {
    // Hard right
    leftOn = false;
    rightOn = true;
  }

  // Apply panning based on voice type
  if (v.type == VOICE_4OP) {
    // For 4-op voices, apply to both physical channels
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t physCh = opl->get4OPControlChannel(v.oplChannel, i);
      opl->setPanning(physCh, leftOn, rightOn);
    }
  } else {
    // For 2-op voices, apply directly
    opl->setPanning(v.oplChannel, leftOn, rightOn);
  }
}

void OPL3Synth::noteOn(uint8_t ch, uint8_t key, uint8_t vel, uint32_t tick) {
  bool isDrum = (ch == 9);

  // Kill any existing note with same channel+key before starting new one
  // This prevents duplicate voices and ensures proper noteOn/noteOff pairing
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type != VOICE_FREE &&
        voices_[i].midiCh == ch &&
        voices_[i].midiKey == key) {
      // Commented out - can be verbose during normal playback
      // Serial.print("DUPLICATE KILLED: Ch");
      // Serial.print(ch);
      // Serial.print(" Key");
      // Serial.print(key);
      // Serial.print(" Tick");
      // Serial.println(tick);
      freeVoice(i);
      break;
    }
  }

  if (isDrum) {
    // Allocate drum channel
    int vid = allocateDrumChannel();
    if (vid < 0) {
      #if DEBUG_SERIAL_ENABLED
      Serial.print("DRUM DROP: Ch");
      Serial.print(ch);
      Serial.print(" Key");
      Serial.print(key);
      Serial.print(" Tick");
      Serial.print(tick);
      Serial.print(" VoicesUsed:");
      uint8_t used = 0;
      for (int i=0; i<MAX_VOICES; ++i) if (voices_[i].type != VOICE_FREE) used++;
      Serial.println(used);
      #endif
      return;
    }

    Voice &v = voices_[vid];
    v.midiCh = ch;
    v.midiKey = key;
    v.velocity = vel;
    v.startTick = tick;
    v.pendingOff = false;

    // Apply drum instrument and get its transpose pitch
    uint8_t transpose = applyDrumInstrument(v.oplChannel, key, vel);
    v.drumTranspose = transpose; // Store for later use if needed

    // Drums play at their transpose pitch (not the MIDI note number)
    uint8_t octave = transpose / 12;
    uint8_t note = transpose % 12;
    opl->playNote(v.oplChannel, octave, note);
  } else {
    // Melodic voice
    bool want4op = true;  // Everyone wants 4-op if available
    int vid = allocateVoice(ch, key, want4op);
    if (vid < 0) {
      #if DEBUG_SERIAL_ENABLED
      Serial.print("NOTE DROP: Ch");
      Serial.print(ch);
      Serial.print(" Key");
      Serial.print(key);
      Serial.print(" Prog");
      Serial.print(ch_[ch].program);
      Serial.print(" Tick");
      Serial.print(tick);

      uint8_t used2op = 0, used4op = 0;
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type == VOICE_2OP) used2op++;
        else if (voices_[i].type == VOICE_4OP) used4op++;
      }

      Serial.print(" VoicesUsed:");
      Serial.print(used2op + used4op);
      Serial.print("/");
      Serial.print(MAX_VOICES);
      Serial.print(" (2op:");
      Serial.print(used2op);
      Serial.print(" 4op:");
      Serial.print(used4op);
      Serial.print(") Prefer4op:");
      Serial.println(prefer4opForProgram(ch_[ch].program) ? "Yes" : "No");
      #endif
      return;
    }

    Voice &v = voices_[vid];
    v.midiCh = ch;
    v.midiKey = key;
    v.velocity = vel;
    v.startTick = tick;
    v.pendingOff = false;

    applyInstrument(v, ch);
    applyVolume(v, ch);
    applyPanning(v, ch);
    applyPitch(v, ch, ch_[ch].pitchBend);
  }
}

void OPL3Synth::noteOff(uint8_t ch, uint8_t key, uint8_t vel) {
  bool found = false;
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type != VOICE_FREE &&
        voices_[i].midiCh == ch &&
        voices_[i].midiKey == key) {
      if (ch_[ch].sustain) {
        voices_[i].pendingOff = true;
      } else {
        freeVoice(i);
      }
      found = true;
      break;  // Only free the first matching voice
    }
  }

  // Commented out - can happen normally in some MIDI files
  // if (!found) {
  //   Serial.print("NOTEOFF ORPHAN: Ch");
  //   Serial.print(ch);
  //   Serial.print(" Key");
  //   Serial.print(key);
  //   Serial.print(" (no matching NoteOn found)");
  //   Serial.println();
  // }
}

void OPL3Synth::programChange(uint8_t ch, uint8_t pg) {
  ch_[ch].program = pg;

  // Preload instruments into memory (not drum channel)
  if (ch != 9) {
    uint8_t program = min(pg, (uint8_t)127);
    ch_[ch].instrument2op = opl->loadInstrument(Instruments2OP::midiInstruments[program]);
    ch_[ch].instrument4op = opl->loadInstrument4OP(Instruments4OP::midiInstruments[program]);
  }
}

void OPL3Synth::controlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  switch (cc) {
    case 7:  // Volume
      ch_[ch].volume = log(max(1.0f, (float)val)) / log(127.0f);
      // Update active voices
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type != VOICE_FREE && voices_[i].midiCh == ch) {
          applyVolume(voices_[i], ch);
        }
      }
      break;
    case 10: // Pan
      ch_[ch].pan = val;
      // Update active voices
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type != VOICE_FREE && voices_[i].midiCh == ch) {
          applyPanning(voices_[i], ch);
        }
      }
      break;
    case 64: // Sustain
      ch_[ch].sustain = (val >= 64);
      if (!ch_[ch].sustain) {
        // Release pending notes
        for (int i=0; i<MAX_VOICES; ++i) {
          if (voices_[i].type != VOICE_FREE &&
              voices_[i].midiCh == ch &&
              voices_[i].pendingOff) {
            freeVoice(i);
          }
        }
      }
      break;
    case 123: // All notes off
      for (int i=0; i<MAX_VOICES; ++i) {
        if (voices_[i].type != VOICE_FREE && voices_[i].midiCh == ch) {
          freeVoice(i);
        }
      }
      break;
  }
}

void OPL3Synth::pitchBend(uint8_t ch, int16_t bend) {
  ch_[ch].pitchBend = bend;

  // Apply pitch bend to all active voices on this channel
  for (int i = 0; i < MAX_VOICES; ++i) {
    if (voices_[i].type != VOICE_FREE &&
        voices_[i].midiCh == ch &&
        !voices_[i].pendingOff) {  // Don't bend notes in release phase
      applyPitch(voices_[i], ch, bend);
    }
  }
}

void OPL3Synth::channelPressure(uint8_t ch, uint8_t value) {
  // Not implemented
}

uint8_t OPL3Synth::getVoicesUsed() const {
  uint8_t used = 0;
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type != VOICE_FREE) used++;
  }
  return used;
}

void OPL3Synth::printVoiceStats() const {
  #if DEBUG_SERIAL_ENABLED
  uint8_t used2op = 0, used4op = 0;
  for (int i=0; i<MAX_VOICES; ++i) {
    if (voices_[i].type == VOICE_2OP) used2op++;
    else if (voices_[i].type == VOICE_4OP) used4op++;
  }
  Serial.print("VOICES: ");
  Serial.print(used2op + used4op);
  Serial.print("/");
  Serial.print(MAX_VOICES);
  Serial.print(" (2op:");
  Serial.print(used2op);
  Serial.print(" 4op:");
  Serial.print(used4op);
  Serial.println(")");
  #endif
}
