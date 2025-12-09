#ifndef PLAYER_CONFIG_H
#define PLAYER_CONFIG_H

#include <Audio.h>

// Forward declarations
class OPL3Synth;
class FileSource;
class DrumSamplerV2;
class NESAPUEmulator;
class GameBoyAPU;
class GenesisBoard;
class DACPrerenderer;
class AudioStreamSPC;
class AudioStreamDACPrerender;

/**
 * PlayerConfig - Dependency Injection Container
 *
 * Contains all external dependencies that players need.
 * Passed to player constructors to avoid global extern access.
 *
 * Design Goals:
 * - Explicit dependency declaration
 * - Easy to test (can inject mocks)
 * - No hidden globals
 * - Single source of truth
 *
 * Usage:
 *   PlayerConfig config;
 *   config.opl3 = g_opl3;
 *   config.fileSource = g_fileSource;
 *   // ... set all required fields
 *
 *   MidiPlayer* player = new MidiPlayer(config);
 */
struct PlayerConfig {
    // ============================================
    // REQUIRED DEPENDENCIES (must be non-null)
    // ============================================

    /**
     * OPL3 synthesizer for FM sound generation
     * Used by: MIDI, VGM, DRO, IMF, RAD players
     * Not used by: SPC (generates PCM directly)
     */
    OPL3Synth* opl3 = nullptr;

    /**
     * File source abstraction (SD, USB, Floppy)
     * Used by: All players for file I/O
     */
    FileSource* fileSource = nullptr;

    // ============================================
    // AUDIO ROUTING (must be non-null)
    // ============================================

    /**
     * Main audio mixers (channel 0 = OPL3, channel 1 = PCM/APU, channel 2 = Drums)
     */
    AudioMixer4* mixerLeft = nullptr;
    AudioMixer4* mixerRight = nullptr;

    /**
     * Submixer for channel 1 (DAC/NES premixer on ch0, SPC on ch1, GB on ch2)
     * VGM and SPC players use this instead of main mixer
     */
    AudioMixer4* mixerChannel1Left = nullptr;
    AudioMixer4* mixerChannel1Right = nullptr;

    /**
     * DAC/NES Pre-mixer (combines DAC Prerender, NES APU, FM9 audio)
     * Channel 0: DAC Prerender (Genesis VGM PCM)
     * Channel 1: NES APU (NES VGM)
     * Channel 2: Unused
     * Channel 3: FM9 audio pre-mixer output
     * Output feeds into mixerChannel1 channel 0
     * VGMPlayer controls muting here for DAC/NES, not on submixer
     */
    AudioMixer4* dacNesMixerLeft = nullptr;
    AudioMixer4* dacNesMixerRight = nullptr;

    /**
     * FM9 Audio Pre-mixer (combines WAV and MP3 streams)
     * Channel 0: FM9 WAV stream
     * Channel 1: FM9 MP3 stream
     * Output feeds into dacNesMixer channel 3
     * FM9Player controls muting here for WAV/MP3 individually
     */
    AudioMixer4* fm9AudioMixerLeft = nullptr;
    AudioMixer4* fm9AudioMixerRight = nullptr;

    /**
     * Fade mixers (for VGM loop fadeout and muting)
     * Channel 0 = main signal
     */
    AudioMixer4* fadeMixerLeft = nullptr;
    AudioMixer4* fadeMixerRight = nullptr;

    /**
     * Final mixers (dry + wet reverb blend)
     * Used by: MIDI player for reverb effect
     */
    AudioMixer4* finalMixerLeft = nullptr;
    AudioMixer4* finalMixerRight = nullptr;

    /**
     * Reverb effects (MIDI only)
     */
    AudioEffectFreeverb* reverbLeft = nullptr;
    AudioEffectFreeverb* reverbRight = nullptr;

    // ============================================
    // OPTIONAL DEPENDENCIES (can be nullptr)
    // ============================================

    /**
     * PCM drum sampler (MIDI channel 10)
     * If nullptr, MIDI player uses FM drums instead
     */
    DrumSamplerV2* drumSampler = nullptr;

    /**
     * NES APU emulator (for VGM NES APU files)
     * Created once at startup, reused by VGMPlayer
     * If nullptr, NES APU VGMs cannot be played
     */
    NESAPUEmulator* nesAPU = nullptr;

    /**
     * Game Boy DMG APU emulator (for VGM Game Boy files)
     * Created once at startup, reused by VGMPlayer
     * If nullptr, Game Boy VGMs cannot be played
     */
    GameBoyAPU* gbAPU = nullptr;

    /**
     * Genesis synthesizer board (YM2612 + SN76489)
     * External hardware board for Sega Genesis/Mega Drive VGMs
     * If nullptr, Genesis VGMs cannot be played
     */
    GenesisBoard* genesisBoard = nullptr;

    /**
     * DAC pre-renderer (for Genesis VGM PCM playback)
     * Pre-renders entire DAC stream to temp file before playback
     * Solves timing issues with dense PCM that real-time emulation cannot handle
     * If nullptr, falls back to hardware DAC (may glitch on dense PCM)
     */
    DACPrerenderer* dacPrerenderer = nullptr;

    /**
     * DAC prerender audio stream (for pre-rendered Genesis DAC playback)
     * Plays back pre-rendered DAC file with perfect timing synchronization
     * Created once at startup, reused by VGMPlayer
     * If nullptr, falls back to hardware DAC (may glitch on dense PCM)
     */
    AudioStreamDACPrerender* dacPrerenderStream = nullptr;

    /**
     * SPC audio stream (for SNES SPC files)
     * Created once at startup, reused by SPCPlayer
     * If nullptr, SPC files cannot be played
     */
    AudioStreamSPC* spcAudioStream = nullptr;

    // ============================================
    // CONFIGURATION FLAGS
    // ============================================

    /**
     * Enable stereo crossfeed for MIDI playback
     * (softer panning, more natural stereo image)
     */
    bool crossfeedEnabled = true;

    /**
     * Enable reverb effect for MIDI playback
     * (adds ambience and depth)
     */
    bool reverbEnabled = true;

    /**
     * VGM loop configuration
     * 0 = loop forever
     * 1+ = fade after N loops
     */
    uint8_t maxLoopsBeforeFade = 2;

    /**
     * VGM fade duration in seconds
     */
    float fadeDurationSeconds = 7.0f;

    /**
     * Enable NES APU filters (affects VGM playback)
     * false = raw APU output (more authentic)
     * true = filtered output (smoother but less accurate)
     */
    bool nesFiltersEnabled = false;

    /**
     * Enable SPC gaussian filter
     * false = raw SPC output
     * true = filtered output (more authentic to SNES hardware)
     */
    bool spcFilterEnabled = false;

    // ============================================
    // VALIDATION
    // ============================================

    /**
     * Validate that all required dependencies are set
     *
     * @return true if config is valid for creating players
     *
     * Checks:
     * - opl3 != nullptr (except for SPC which doesn't need it)
     * - fileSource != nullptr
     * - All core mixer pointers != nullptr
     *
     * Note: Reverb was removed to save RAM. fm9AudioMixer is optional.
     */
    bool isValid() const {
        bool valid = fileSource != nullptr &&
                     mixerLeft != nullptr &&
                     mixerRight != nullptr &&
                     mixerChannel1Left != nullptr &&
                     mixerChannel1Right != nullptr &&
                     dacNesMixerLeft != nullptr &&
                     dacNesMixerRight != nullptr &&
                     fadeMixerLeft != nullptr &&
                     fadeMixerRight != nullptr &&
                     finalMixerLeft != nullptr &&
                     finalMixerRight != nullptr;
        // Note: reverbLeft/Right removed (reverb disabled to save RAM)
        // Note: fm9AudioMixerLeft/Right are optional (FM9 audio is optional)

        return valid;
    }

    /**
     * Check if OPL3 is available
     * (SPC player doesn't need OPL3, others do)
     */
    bool hasOPL3() const {
        return opl3 != nullptr;
    }

    /**
     * Check if drum sampler is available
     */
    bool hasDrumSampler() const {
        return drumSampler != nullptr;
    }
};

#endif // PLAYER_CONFIG_H
