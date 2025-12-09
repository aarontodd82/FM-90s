#include "audio_system.h"
#include "drum_sampler_v2.h"
#include "opl3_synth.h"

// Static member initialization
float AudioSystem::currentMasterVolume_ = 0.7f;

// ========== Line-In Configuration (Hardware Synthesizers) ==========
// Two-tier volume control:
// 1. SGTL5000 ADC level (0-15) - Hardware analog-to-digital amplification
// 2. Mixer gain (0.0-1.0) - Software digital mixing level

// SGTL5000 line-in ADC levels (0-15 scale)
static constexpr uint8_t OPL3_LINE_IN_LEVEL = 10;      // Hardware ADC gain for OPL3 Duo
static constexpr uint8_t GENESIS_LINE_IN_LEVEL = 10;   // Hardware ADC gain for Genesis (adjust after testing)

// Mixer channel 0 gains (0.0-1.0 scale)
static constexpr float OPL3_LINE_IN_GAIN = 0.9f;       // Mixer software gain for OPL3
static constexpr float GENESIS_LINE_IN_GAIN = 0.9f;    // Mixer software gain for Genesis

bool AudioSystem::initialize(
    const Config& config,
    AudioControlSGTL5000& audioShield,
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight,
    AudioMixer4& finalMixerLeft,
    AudioMixer4& finalMixerRight,
    AudioMixer4& fadeMixerLeft,
    AudioMixer4& fadeMixerRight
) {
    // Note: AudioMemory() must be called by the caller before this function
    // (it requires a compile-time constant, not a runtime variable)
    // Note: Reverb removed to save ~50KB RAM (AudioEffectFreeverb uses 25KB each)

    // Initialize SGTL5000 audio codec
    audioShield.enable();
    audioShield.inputSelect(AUDIO_INPUT_LINEIN);  // Select input FIRST
    audioShield.lineInLevel(10);  // Line input level (0-15) - set AFTER selecting input
    audioShield.volume(config.masterVolume);

    // Store current master volume for save/restore
    currentMasterVolume_ = config.masterVolume;

    // Configure mixer gains
    configureMixers(config, mixerLeft, mixerRight);

    // Reverb removed - configure final mixer for direct passthrough
    finalMixerLeft.gain(0, 1.0f);   // Direct signal at 100%
    finalMixerLeft.gain(1, 0.0f);   // No reverb
    finalMixerRight.gain(0, 1.0f);  // Direct signal at 100%
    finalMixerRight.gain(1, 0.0f);  // No reverb

    // Configure crossfeed
    enableCrossfeed(mixerLeft, mixerRight, config.enableCrossfeed);

    // Configure fade mixers (UNMUTED FOR TESTING - normally starts muted)
    // Players will unmute when starting playback, mute when stopping
    fadeMixerLeft.gain(0, 1.0f);   // Channel 0 = 100% (UNMUTED for testing)
    fadeMixerLeft.gain(1, 0.0f);   // Other channels unused
    fadeMixerLeft.gain(2, 0.0f);
    fadeMixerLeft.gain(3, 0.0f);
    fadeMixerRight.gain(0, 1.0f);  // Channel 0 = 100% (UNMUTED for testing)
    fadeMixerRight.gain(1, 0.0f);  // Other channels unused
    fadeMixerRight.gain(2, 0.0f);
    fadeMixerRight.gain(3, 0.0f);

    return true;
}

void AudioSystem::setPCMGain(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight,
    float gain
) {
    mixerLeft.gain(1, gain);   // PCM left channel
    mixerRight.gain(1, gain);  // PCM right channel
}

void AudioSystem::enableCrossfeed(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight,
    bool enable
) {
    if (enable) {
        // 70% main signal, 30% crossfeed from opposite channel
        mixerLeft.gain(0, 0.56f);   // 70% of 0.8 = 0.56
        mixerLeft.gain(3, 0.24f);   // 30% of 0.8 = 0.24 (crossfeed from right)
        mixerRight.gain(0, 0.56f);  // 70% of 0.8 = 0.56
        mixerRight.gain(3, 0.24f);  // 30% of 0.8 = 0.24 (crossfeed from left)
    } else {
        // 100% main signal, no crossfeed (hard L/R)
        mixerLeft.gain(0, 0.8f);    // Full main signal
        mixerLeft.gain(3, 0.0f);    // No crossfeed
        mixerRight.gain(0, 0.8f);   // Full main signal
        mixerRight.gain(3, 0.0f);   // No crossfeed
    }
}

// Reverb removed to save ~50KB RAM (AudioEffectFreeverb uses 25KB each)
// void AudioSystem::enableReverb(
//     AudioMixer4& finalMixerLeft,
//     AudioMixer4& finalMixerRight,
//     AudioEffectFreeverb& reverbLeft,
//     AudioEffectFreeverb& reverbRight,
//     bool enable
// ) {
//     if (enable) {
//         // Medium reverb - noticeable but not overpowering
//         // Dry signal: 82%
//         finalMixerLeft.gain(0, 0.82f);
//         finalMixerRight.gain(0, 0.82f);
//
//         // Wet signal: 25% for pleasant ambience
//         finalMixerLeft.gain(1, 0.25f);
//         finalMixerRight.gain(1, 0.25f);
//     } else {
//         // 100% dry signal, no reverb
//         finalMixerLeft.gain(0, 1.0f);
//         finalMixerRight.gain(0, 1.0f);
//
//         // Mute wet signal
//         finalMixerLeft.gain(1, 0.0f);
//         finalMixerRight.gain(1, 0.0f);
//     }
// }

void AudioSystem::setDrumGain(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight,
    float gain
) {
    mixerLeft.gain(2, gain);   // Drum sampler left
    mixerRight.gain(2, gain);  // Drum sampler right
}

void AudioSystem::setDrumSamplerEnabled(
    bool enabled,
    DrumSamplerV2* drumSampler,
    OPL3Synth* opl3Synth,
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight
) {
    if (drumSampler) {
        drumSampler->setEnabled(enabled);
    }

    if (enabled) {
        // Enable PCM drums - unmute mixer channels
        mixerLeft.gain(2, 0.40f);
        mixerRight.gain(2, 0.40f);
        // // Serial.println("[AudioSystem] PCM Drum Sampler: ENABLED");
    } else {
        // Disable PCM drums - mute mixer channels
        mixerLeft.gain(2, 0.0f);
        mixerRight.gain(2, 0.0f);
        // // Serial.println("[AudioSystem] PCM Drum Sampler: DISABLED (using FM drums)");
    }

    // Tell OPL3 synth whether to use PCM or FM drums
    // (FM drums reserve 6 channels, PCM drums free them)
    if (opl3Synth) {
        opl3Synth->setDrumSamplerEnabled(enabled);
    }
}

void AudioSystem::setMasterVolume(
    AudioControlSGTL5000& audioShield,
    float volume
) {
    audioShield.volume(volume);
    currentMasterVolume_ = volume;
}

float AudioSystem::getMasterVolume() {
    return currentMasterVolume_;
}

void AudioSystem::setFadeGain(
    AudioMixer4& fadeMixerLeft,
    AudioMixer4& fadeMixerRight,
    float gain
) {
    // // Serial.printf("AudioSystem::setFadeGain called with gain=%f\n", gain);
    // Simple fade control - just adjust the single channel gain
    // This affects the digital signal before it splits to Bluetooth and line-out
    fadeMixerLeft.gain(0, gain);
    fadeMixerRight.gain(0, gain);
    // // Serial.println("AudioSystem::setFadeGain completed");
}

// ========================================
// Private Helper Methods
// ========================================

void AudioSystem::configureMixers(
    const Config& config,
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight
) {
    // Channel 0: OPL3 input
    mixerLeft.gain(0, config.opl3Gain);
    mixerRight.gain(0, config.opl3Gain);

    // Channel 1: FM90S PCM
    mixerLeft.gain(1, config.pcmGain);
    mixerRight.gain(1, config.pcmGain);

    // Channel 2: Drum sampler (will be set when drum sampler initializes)
    mixerLeft.gain(2, config.drumGain);
    mixerRight.gain(2, config.drumGain);

    // Channel 3: Crossfeed (will be set by enableCrossfeed)
    mixerLeft.gain(3, 0.0f);
    mixerRight.gain(3, 0.0f);
}

// Reverb removed to save ~50KB RAM
// void AudioSystem::configureReverb(
//     AudioEffectFreeverb& reverbLeft,
//     AudioEffectFreeverb& reverbRight
// ) {
//     // Configure reverb for medium hall - noticeable but pleasant
//     reverbLeft.roomsize(0.6f);   // Medium hall
//     reverbLeft.damping(0.55f);   // Moderate damping for natural decay
//     reverbRight.roomsize(0.6f);
//     reverbRight.damping(0.55f);
// }

// ========================================
// Line-In Control (Hardware Synthesizers)
// ========================================

void AudioSystem::muteLineIn(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight
) {
    mixerLeft.gain(0, 0.0f);   // Mute line-in (main mixer channel 0)
    mixerRight.gain(0, 0.0f);
}

void AudioSystem::unmuteLineInForOPL3(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight
) {
    // Unmute mixer channel 0 with OPL3-specific gain
    mixerLeft.gain(0, OPL3_LINE_IN_GAIN);
    mixerRight.gain(0, OPL3_LINE_IN_GAIN);

    // Note: SGTL5000 lineInLevel should be set separately via setLineInLevel()
}

void AudioSystem::unmuteLineInForGenesis(
    AudioMixer4& mixerLeft,
    AudioMixer4& mixerRight
) {
    // Unmute mixer channel 0 with Genesis-specific gain
    mixerLeft.gain(0, GENESIS_LINE_IN_GAIN);
    mixerRight.gain(0, GENESIS_LINE_IN_GAIN);

    // Note: SGTL5000 lineInLevel should be set separately via setLineInLevel()
}

void AudioSystem::setLineInLevel(
    AudioControlSGTL5000& audioShield,
    uint8_t level
) {
    // Clamp to valid range
    if (level > 15) level = 15;

    audioShield.lineInLevel(level);
}
