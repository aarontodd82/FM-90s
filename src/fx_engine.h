#pragma once

#include <Arduino.h>
#include <cstdint>

/**
 * FXEngine - Effects automation timeline for FM9 files
 *
 * Parses FX JSON from FM9 files and applies timed effect changes
 * to the Teensy Audio system.
 *
 * STATUS: SKELETON IMPLEMENTATION
 * Effect application is stubbed with Serial debug output.
 * Real effect control will be added in a future phase.
 *
 * Supported Effects (future):
 * - reverb: room_size, damping, wet_mix
 * - delay: time_ms, feedback, wet_mix
 * - chorus: rate_hz, depth, wet_mix
 * - eq_low/mid/high: gain_db
 * - master_volume: level
 */

// Forward declaration
class AudioSystem;

/**
 * Single FX event in the timeline
 */
struct FXEvent {
    uint32_t time_ms;           // When to apply this event

    // Reverb parameters (NaN = no change)
    float reverb_room_size;
    float reverb_damping;
    float reverb_wet;
    bool reverb_enabled;
    bool reverb_changed;        // True if any reverb param changed

    // Delay parameters (TODO: implement AudioEffectDelay)
    float delay_time_ms;
    float delay_feedback;
    float delay_wet;
    bool delay_enabled;
    bool delay_changed;

    // Chorus parameters (TODO: implement AudioEffectChorus)
    float chorus_rate;
    float chorus_depth;
    float chorus_wet;
    bool chorus_enabled;
    bool chorus_changed;

    // EQ parameters (TODO: implement AudioFilterBiquad chains)
    float eq_low_gain;
    float eq_mid_gain;
    float eq_mid_freq;
    float eq_high_gain;
    bool eq_changed;

    // Master volume
    float master_volume;
    bool master_volume_changed;

    FXEvent() {
        time_ms = 0;
        reverb_room_size = NAN;
        reverb_damping = NAN;
        reverb_wet = NAN;
        reverb_enabled = false;
        reverb_changed = false;
        delay_time_ms = NAN;
        delay_feedback = NAN;
        delay_wet = NAN;
        delay_enabled = false;
        delay_changed = false;
        chorus_rate = NAN;
        chorus_depth = NAN;
        chorus_wet = NAN;
        chorus_enabled = false;
        chorus_changed = false;
        eq_low_gain = NAN;
        eq_mid_gain = NAN;
        eq_mid_freq = NAN;
        eq_high_gain = NAN;
        eq_changed = false;
        master_volume = NAN;
        master_volume_changed = false;
    }
};

class FXEngine {
public:
    FXEngine();
    ~FXEngine();

    /**
     * Load FX timeline from JSON string
     *
     * Expected format:
     * {
     *   "version": 1,
     *   "events": [
     *     { "time_ms": 0, "effects": { "reverb": { "enabled": true, ... } } },
     *     ...
     *   ]
     * }
     *
     * @param json Null-terminated JSON string
     * @param length Length of JSON string
     * @return true if parsed successfully
     */
    bool loadFromJson(const char* json, size_t length);

    /**
     * Clear all loaded events
     */
    void clear();

    /**
     * Reset playback to beginning
     */
    void reset();

    /**
     * Update FX state based on current playback position
     * Call this from player's update() method
     *
     * @param position_ms Current playback position in milliseconds
     */
    void update(uint32_t position_ms);

    /**
     * Check if any events were loaded
     */
    bool hasEvents() const { return eventCount_ > 0; }

    /**
     * Get number of loaded events
     */
    size_t getEventCount() const { return eventCount_; }

private:
    // Event storage (simple fixed array for now)
    static const size_t MAX_EVENTS = 64;
    FXEvent events_[MAX_EVENTS];
    size_t eventCount_;
    size_t currentEventIndex_;

    // Current effect state (cumulative)
    FXEvent currentState_;

    // Parsing helpers
    bool parseJson(const char* json, size_t length);
    bool parseEvent(const char* eventJson, size_t length, FXEvent& event);
    float parseFloat(const char* str, size_t maxLen);
    bool parseBool(const char* str, size_t maxLen);

    // Effect application (SKELETON - prints to Serial)
    void applyEvent(const FXEvent& event);
    void applyReverb(const FXEvent& event);
    void applyDelay(const FXEvent& event);
    void applyChorus(const FXEvent& event);
    void applyEQ(const FXEvent& event);
    void applyMasterVolume(const FXEvent& event);
};
