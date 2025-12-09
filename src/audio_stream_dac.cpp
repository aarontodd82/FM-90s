/**
 * @file audio_stream_dac.cpp
 * @brief Implementation of AudioStreamDAC
 *
 * CRITICAL: This file is a separate translation unit to avoid ODR violations
 * with the Audio Library's update list registration system.
 *
 * @author Claude + Aaron
 * @date January 2025
 */

#include <Audio.h>  // MUST be first to ensure consistent AudioStream definition
#include "audio_stream_dac.h"
#include "dac_emulator.h"

//=============================================================================
// AudioStreamDAC implementation
//=============================================================================

AudioStreamDAC::AudioStreamDAC(DACEmulator* emulator)
    : AudioStream(0, nullptr)  // 0 inputs means we're a source
    , emulator_(emulator)
    , firstUpdate_(true)
    , updateCount_(0)
    , ticks_(0) {

    Serial.println("[AudioStreamDAC] Constructor - registering with Audio Library");
    Serial.printf("[AudioStreamDAC] Object created at address 0x%08X\n", (uint32_t)this);

    // DIAGNOSTIC: Log which translation unit this is compiled in
    Serial.printf("[AudioStreamDAC] Compiled in: %s\n", __FILE__);

    Serial.printf("[AudioStreamDAC] Constructor complete, this=%p\n", this);
}

void AudioStreamDAC::setEmulator(DACEmulator* emulator) {
    emulator_ = emulator;
    Serial.print("[AudioStreamDAC] Emulator pointer set to: 0x");
    Serial.println((uint32_t)emulator_, HEX);
}

void AudioStreamDAC::update() {
    updateCount_++;  // Increment the member variable counter
    ticks_++;        // Volatile counter for diagnostic

    // NO Serial.print in ISR context - check counters from main loop instead

    // Always allocate blocks
    audio_block_t* left = allocate();
    audio_block_t* right = allocate();

    if (!left || !right) {
        // Can't allocate - cleanup and return
        if (left) release(left);
        if (right) release(right);
        return;
    }

    // Fill the blocks with audio data or silence
    if (!emulator_) {
        // No emulator connected - output silence
        memset(left->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(right->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    } else {
        // Emulator connected - get audio data
        emulator_->fillAudioBuffer(left->data, right->data, AUDIO_BLOCK_SAMPLES);
    }

    // Always transmit blocks
    transmit(left, 0);
    transmit(right, 1);

    // Release blocks
    release(left);
    release(right);

    firstUpdate_ = false;
}
