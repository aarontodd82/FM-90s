#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// ========================================
// Debug Output Configuration
// ========================================
//
// Serial.print() is BLOCKING and affects audio timing!
// During complex MIDI files, NOTE DROP messages can spam 50-100+ times/second
// At 115200 baud, each message takes ~4ms = up to 400ms/second wasted on Serial!
//
// Set DEBUG_SERIAL_ENABLED to false to DISABLE ALL Serial.print() calls
// Only enable specific flags when actively debugging that subsystem

#define DEBUG_SERIAL_ENABLED false           // MASTER KILL SWITCH - false = NO serial output at all
#define DEBUG_PERFORMANCE_STATS false        // Screen update performance monitoring
#define DEBUG_VGM_PLAYBACK false              // VGM command processing (VERY CHATTY - causes lockups!)
#define DEBUG_BLUETOOTH false                 // Bluetooth communication (TX/RX logging)
#define DEBUG_FILE_LOADING false              // File parsing details (VGM/DRO/IMF/RAD headers)
#define DEBUG_AUDIO_SYSTEM false              // Audio routing, mixers, effects
#define DEBUG_PLAYBACK false                  // Playback start/stop/state changes

// Convenience: Enable all debug flags at once (for development)
// Uncomment this to override all flags above
// #define DEBUG_ALL

// ========================================
// Debug Macros
// ========================================
// Use these instead of Serial.print() directly
// They automatically respect DEBUG_SERIAL_ENABLED

#if DEBUG_SERIAL_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif // DEBUG_CONFIG_H
