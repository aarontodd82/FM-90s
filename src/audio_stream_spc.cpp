#include <Audio.h>  // MUST be first to ensure consistent AudioStream definition
#include "audio_stream_spc.h"
#include "spc_player.h"

//=============================================================================
// AudioStreamSPC implementation
//=============================================================================

AudioStreamSPC::AudioStreamSPC(SPCPlayer* player)
    : AudioStream(0, nullptr)  // 0 inputs means we're a source
    , player_(player)
    , firstUpdate_(true)
    , updateCount_(0)
    , ticks_(0) {

    Serial.println("[AudioStreamSPC] Constructor - registering with Audio Library");
    Serial.printf("[AudioStreamSPC] Object created at address 0x%08X\n", (uint32_t)this);

    // DIAGNOSTIC: Log which translation unit this is compiled in
    Serial.printf("[AudioStreamSPC] Compiled in: %s\n", __FILE__);

    Serial.printf("[AudioStreamSPC] Constructor complete, this=%p\n", this);
}

void AudioStreamSPC::setPlayer(SPCPlayer* player) {
    player_ = player;
    Serial.print("[AudioStreamSPC] Player pointer set to: 0x");
    Serial.println((uint32_t)player_, HEX);
}

void AudioStreamSPC::update() {
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
    if (!player_) {
        // No player connected - output silence
        memset(left->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(right->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    } else {
        // Player connected - get audio data
        player_->fillAudioBuffer(left->data, right->data, AUDIO_BLOCK_SAMPLES);
    }

    // Always transmit blocks
    transmit(left, 0);
    transmit(right, 1);

    // Release blocks
    release(left);
    release(right);

    firstUpdate_ = false;
}