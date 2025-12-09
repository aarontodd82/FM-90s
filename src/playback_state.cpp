#include "playback_state.h"

// Static member initialization
PlaybackState* PlaybackState::instance = nullptr;

// Global convenience pointer
PlaybackState* g_playbackState = PlaybackState::getInstance();
