#ifndef AUDIO_GLOBALS_H
#define AUDIO_GLOBALS_H

#include <Audio.h>

/**
 * Global Audio Objects
 *
 * These audio objects MUST be global due to Teensy Audio Library requirements.
 * They are defined in main.cpp and declared as extern here for access by other modules.
 */

// Audio I/O
extern AudioInputI2S            i2sIn;
extern AudioOutputI2S           i2sOut;
extern AudioControlSGTL5000     audioShield;

// Mixers
extern AudioMixer4              mixerLeft;
extern AudioMixer4              mixerRight;
extern AudioMixer4              finalMixerLeft;
extern AudioMixer4              finalMixerRight;
extern AudioMixer4              fadeMixerLeft;    // Final fade stage (VGM loop fadeout)
extern AudioMixer4              fadeMixerRight;   // Final fade stage (VGM loop fadeout)

// DAC/NES Pre-mixer (combines DAC Prerender and NES APU before submixer)
// Solves conflict where both sources were connected to same submixer channel
// Channel 0: DAC Prerender (Genesis VGM PCM)
// Channel 1: NES APU (NES VGM)
// Channel 2: S3M PCM
// Channel 3: FM9 WAV (embedded audio)
// Output feeds into mixerChannel1Left/Right channel 0
extern AudioMixer4              dacNesMixerLeft;
extern AudioMixer4              dacNesMixerRight;

// FM9 WAV player (embedded audio from FM9 extended VGM files)
// Uses custom AudioStream with sync support and PSRAM buffering
class AudioStreamFM9Wav;  // Forward declaration
extern AudioStreamFM9Wav        g_fm9WavStream_obj;
extern AudioStreamFM9Wav*       g_fm9WavStream;

// Effects
extern AudioEffectFreeverb      reverbLeft;
extern AudioEffectFreeverb      reverbRight;

// Persistent AudioConnection pointers for dynamic audio sources
// These stay allocated for the entire program lifetime to avoid
// ISR crashes from creating/destroying connections dynamically
extern AudioConnection*         patchCordNESAPULeft;   // NES APU left → mixerLeft ch1
extern AudioConnection*         patchCordNESAPURight;  // NES APU right → mixerRight ch1
extern AudioConnection*         patchCordSPCLeft;      // SPC left → mixerLeft ch1
extern AudioConnection*         patchCordSPCRight;     // SPC right → mixerRight ch1

#endif // AUDIO_GLOBALS_H
