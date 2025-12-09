#pragma once
#include <Arduino.h>

// Basic MIDI event types
enum class MidiEventType : uint8_t {
  NoteOn, NoteOff, ControlChange, ProgramChange,
  ChannelPressure, PitchBend, MetaTempo, EndOfTrack, Unknown
};

// A decoded MIDI event with absolute tick time
struct MidiEvent {
  uint32_t tick = 0;
  MidiEventType type = MidiEventType::Unknown;
  uint8_t  channel = 0;
  uint8_t  key = 0;         // note number for note events
  uint8_t  velocity = 0;    // velocity for note on/off
  uint8_t  value1 = 0;      // CC number, Program #, Channel Pressure
  uint8_t  value2 = 0;      // CC value
  int16_t  pitchBend = 0;   // -8192..8191
  uint32_t tempoUSQ = 500000; // for MetaTempo
};