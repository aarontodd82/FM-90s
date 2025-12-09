#ifndef AUDIO_SYSTEM_H
#define AUDIO_SYSTEM_H

#include <Arduino.h>
#include <Audio.h>

/**
 * AudioSystem - Centralized audio configuration and control
 *
 * Manages the Teensy audio system including:
 * - SGTL5000 audio board initialization
 * - Stereo crossfeed (softer panning for MIDI)
 * - Reverb effect (ambience for MIDI)
 * - Mixer gain control
 *
 * Note: Audio objects and connections MUST remain global due to Teensy Audio
 * library requirements. This class provides initialization and control methods.
 */
class AudioSystem {
public:
    struct Config {
        bool enableCrossfeed = true;       // Softer stereo panning (MIDI)
        bool enableReverb = true;          // Reverb effect (MIDI)
        float masterVolume = 0.7f;         // Overall output volume (0.0-1.0)
        float opl3Gain = 0.8f;             // OPL3 mixer gain (0.0-1.0)
        float pcmGain = 0.0f;              // FM90S PCM gain (0.0-1.0)
        float drumGain = 0.4f;             // Drum sampler gain (0.0-1.0)
        // Note: AudioMemory() must be called separately before initialize()
    };

    /**
     * Initialize the audio board and configure all audio paths
     * Note: Reverb removed to save ~50KB RAM
     *
     * @param config Audio system configuration
     * @param audioShield Reference to SGTL5000 control object
     * @param mixerLeft Reference to left mixer
     * @param mixerRight Reference to right mixer
     * @param finalMixerLeft Reference to left final mixer (now passthrough, reverb removed)
     * @param finalMixerRight Reference to right final mixer (now passthrough, reverb removed)
     * @return true if initialization successful
     */
    static bool initialize(
        const Config& config,
        AudioControlSGTL5000& audioShield,
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight,
        AudioMixer4& finalMixerLeft,
        AudioMixer4& finalMixerRight,
        AudioMixer4& fadeMixerLeft,
        AudioMixer4& fadeMixerRight
    );

    // PCM mixer control (for FM90S player)
    static void setPCMGain(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight,
        float gain
    );

    // Stereo crossfeed control (for softer MIDI panning)
    static void enableCrossfeed(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight,
        bool enable
    );

    // Reverb removed to save ~50KB RAM (AudioEffectFreeverb uses 25KB each)
    // static void enableReverb(
    //     AudioMixer4& finalMixerLeft,
    //     AudioMixer4& finalMixerRight,
    //     AudioEffectFreeverb& reverbLeft,
    //     AudioEffectFreeverb& reverbRight,
    //     bool enable
    // );

    // Drum sampler gain control
    static void setDrumGain(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight,
        float gain
    );

    // Drum sampler enable/disable (runtime toggle between PCM and FM drums)
    static void setDrumSamplerEnabled(
        bool enabled,
        class DrumSamplerV2* drumSampler,
        class OPL3Synth* opl3Synth,
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight
    );

    // Master volume control
    static void setMasterVolume(
        AudioControlSGTL5000& audioShield,
        float volume
    );

    // Get current master volume
    static float getMasterVolume();

    // Fade control (for VGM loop fadeout - affects both Bluetooth and line-out)
    static void setFadeGain(
        AudioMixer4& fadeMixerLeft,
        AudioMixer4& fadeMixerRight,
        float gain  // 0.0 = silent, 1.0 = full volume
    );

    // ========== Line-In Control (Hardware Synthesizers: OPL3 / Genesis) ==========

    /**
     * Mute the line-in (main mixer channel 0)
     * Use when switching to software emulators (NES APU, SPC, MOD, etc.)
     */
    static void muteLineIn(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight
    );

    /**
     * Unmute line-in for OPL3 hardware (main mixer channel 0)
     * Uses standard OPL3 gain level (0.8f)
     */
    static void unmuteLineInForOPL3(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight
    );

    /**
     * Unmute line-in for Genesis hardware (main mixer channel 0)
     * May use different gain level if Genesis outputs different analog level
     */
    static void unmuteLineInForGenesis(
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight
    );

    /**
     * Set SGTL5000 line-in level (amplification)
     * @param audioShield SGTL5000 control object
     * @param level Line-in level (0-15, 0=quietest, 15=loudest)
     */
    static void setLineInLevel(
        AudioControlSGTL5000& audioShield,
        uint8_t level  // 0-15
    );

private:
    static float currentMasterVolume_;  // Track current volume for save/restore
    // Helper to configure mixer channels
    static void configureMixers(
        const Config& config,
        AudioMixer4& mixerLeft,
        AudioMixer4& mixerRight
    );

    // Reverb removed to save ~50KB RAM
    // static void configureReverb(
    //     AudioEffectFreeverb& reverbLeft,
    //     AudioEffectFreeverb& reverbRight
    // );
};

#endif // AUDIO_SYSTEM_H
