#include "fx_engine.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

FXEngine::FXEngine()
    : eventCount_(0)
    , currentEventIndex_(0) {
    memset(events_, 0, sizeof(events_));
}

FXEngine::~FXEngine() {
    clear();
}

void FXEngine::clear() {
    eventCount_ = 0;
    currentEventIndex_ = 0;
    currentState_ = FXEvent();
}

void FXEngine::reset() {
    currentEventIndex_ = 0;
    currentState_ = FXEvent();
}

bool FXEngine::loadFromJson(const char* json, size_t length) {
    clear();

    if (!json || length == 0) {
        Serial.println("[FXEngine] No JSON data provided");
        return false;
    }

    Serial.print("[FXEngine] Parsing FX JSON (");
    Serial.print(length);
    Serial.println(" bytes)");

    return parseJson(json, length);
}

void FXEngine::update(uint32_t position_ms) {
    if (eventCount_ == 0) {
        return;  // No events to process
    }

    // Process all events up to current position
    while (currentEventIndex_ < eventCount_ &&
           events_[currentEventIndex_].time_ms <= position_ms) {

        applyEvent(events_[currentEventIndex_]);
        currentEventIndex_++;
    }
}

// ============================================
// JSON Parsing (simple implementation)
// ============================================

// Find a key in JSON and return pointer to its value
static const char* findJsonKey(const char* json, size_t len, const char* key) {
    size_t keyLen = strlen(key);
    const char* end = json + len;

    for (const char* p = json; p < end - keyLen - 2; p++) {
        if (*p == '"') {
            // Check if this is our key
            if (strncmp(p + 1, key, keyLen) == 0 && p[keyLen + 1] == '"') {
                // Find the colon
                const char* colon = p + keyLen + 2;
                while (colon < end && *colon != ':') colon++;
                if (colon < end) {
                    // Skip whitespace after colon
                    colon++;
                    while (colon < end && (*colon == ' ' || *colon == '\t' || *colon == '\n' || *colon == '\r')) colon++;
                    return colon;
                }
            }
        }
    }
    return nullptr;
}

// Find matching closing bracket/brace
static const char* findClosingBracket(const char* start, const char* end, char open, char close) {
    int depth = 1;
    const char* p = start + 1;
    while (p < end && depth > 0) {
        if (*p == open) depth++;
        else if (*p == close) depth--;
        p++;
    }
    return (depth == 0) ? p : nullptr;
}

float FXEngine::parseFloat(const char* str, size_t maxLen) {
    char buf[32];
    size_t len = 0;
    while (len < maxLen && len < sizeof(buf) - 1 &&
           (str[len] == '-' || str[len] == '.' || (str[len] >= '0' && str[len] <= '9'))) {
        buf[len] = str[len];
        len++;
    }
    buf[len] = '\0';
    return (len > 0) ? atof(buf) : NAN;
}

bool FXEngine::parseBool(const char* str, size_t maxLen) {
    if (maxLen >= 4 && strncmp(str, "true", 4) == 0) return true;
    return false;
}

bool FXEngine::parseJson(const char* json, size_t length) {
    // Find "events" array
    const char* eventsPtr = findJsonKey(json, length, "events");
    if (!eventsPtr || *eventsPtr != '[') {
        Serial.println("[FXEngine] No 'events' array found");
        return false;
    }

    const char* arrayEnd = findClosingBracket(eventsPtr, json + length, '[', ']');
    if (!arrayEnd) {
        Serial.println("[FXEngine] Malformed events array");
        return false;
    }

    // Parse each event object
    const char* p = eventsPtr + 1;
    while (p < arrayEnd && eventCount_ < MAX_EVENTS) {
        // Skip whitespace
        while (p < arrayEnd && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;

        if (*p == '{') {
            const char* eventEnd = findClosingBracket(p, arrayEnd, '{', '}');
            if (eventEnd) {
                FXEvent event;
                if (parseEvent(p, eventEnd - p, event)) {
                    events_[eventCount_++] = event;
                }
                p = eventEnd;
            } else {
                break;
            }
        } else {
            p++;
        }
    }

    Serial.print("[FXEngine] Parsed ");
    Serial.print(eventCount_);
    Serial.println(" events");

    // Print event summary
    for (size_t i = 0; i < eventCount_; i++) {
        Serial.print("  Event ");
        Serial.print(i);
        Serial.print(": time=");
        Serial.print(events_[i].time_ms);
        Serial.print("ms");
        if (events_[i].reverb_changed) Serial.print(" [reverb]");
        if (events_[i].delay_changed) Serial.print(" [delay]");
        if (events_[i].chorus_changed) Serial.print(" [chorus]");
        if (events_[i].eq_changed) Serial.print(" [eq]");
        if (events_[i].master_volume_changed) Serial.print(" [volume]");
        Serial.println();
    }

    return eventCount_ > 0;
}

bool FXEngine::parseEvent(const char* eventJson, size_t length, FXEvent& event) {
    event = FXEvent();

    // Parse time_ms
    const char* timePtr = findJsonKey(eventJson, length, "time_ms");
    if (timePtr) {
        event.time_ms = (uint32_t)parseFloat(timePtr, 16);
    }

    // Find "effects" object
    const char* effectsPtr = findJsonKey(eventJson, length, "effects");
    if (!effectsPtr || *effectsPtr != '{') {
        return true;  // Event with just time is valid
    }

    const char* effectsEnd = findClosingBracket(effectsPtr, eventJson + length, '{', '}');
    if (!effectsEnd) return true;

    size_t effectsLen = effectsEnd - effectsPtr;

    // Parse reverb
    const char* reverbPtr = findJsonKey(effectsPtr, effectsLen, "reverb");
    if (reverbPtr && *reverbPtr == '{') {
        const char* reverbEnd = findClosingBracket(reverbPtr, effectsEnd, '{', '}');
        if (reverbEnd) {
            size_t reverbLen = reverbEnd - reverbPtr;

            const char* val = findJsonKey(reverbPtr, reverbLen, "enabled");
            if (val) {
                event.reverb_enabled = parseBool(val, 8);
                event.reverb_changed = true;
            }

            val = findJsonKey(reverbPtr, reverbLen, "room_size");
            if (val) {
                event.reverb_room_size = parseFloat(val, 16);
                event.reverb_changed = true;
            }

            val = findJsonKey(reverbPtr, reverbLen, "damping");
            if (val) {
                event.reverb_damping = parseFloat(val, 16);
                event.reverb_changed = true;
            }

            val = findJsonKey(reverbPtr, reverbLen, "wet_mix");
            if (val) {
                event.reverb_wet = parseFloat(val, 16);
                event.reverb_changed = true;
            }
        }
    }

    // Parse delay (TODO: implement effect)
    const char* delayPtr = findJsonKey(effectsPtr, effectsLen, "delay");
    if (delayPtr && *delayPtr == '{') {
        const char* delayEnd = findClosingBracket(delayPtr, effectsEnd, '{', '}');
        if (delayEnd) {
            size_t delayLen = delayEnd - delayPtr;

            const char* val = findJsonKey(delayPtr, delayLen, "enabled");
            if (val) {
                event.delay_enabled = parseBool(val, 8);
                event.delay_changed = true;
            }

            val = findJsonKey(delayPtr, delayLen, "time_ms");
            if (val) {
                event.delay_time_ms = parseFloat(val, 16);
                event.delay_changed = true;
            }

            val = findJsonKey(delayPtr, delayLen, "feedback");
            if (val) {
                event.delay_feedback = parseFloat(val, 16);
                event.delay_changed = true;
            }

            val = findJsonKey(delayPtr, delayLen, "wet_mix");
            if (val) {
                event.delay_wet = parseFloat(val, 16);
                event.delay_changed = true;
            }
        }
    }

    // Parse chorus (TODO: implement effect)
    const char* chorusPtr = findJsonKey(effectsPtr, effectsLen, "chorus");
    if (chorusPtr && *chorusPtr == '{') {
        const char* chorusEnd = findClosingBracket(chorusPtr, effectsEnd, '{', '}');
        if (chorusEnd) {
            size_t chorusLen = chorusEnd - chorusPtr;

            const char* val = findJsonKey(chorusPtr, chorusLen, "enabled");
            if (val) {
                event.chorus_enabled = parseBool(val, 8);
                event.chorus_changed = true;
            }

            val = findJsonKey(chorusPtr, chorusLen, "rate_hz");
            if (val) {
                event.chorus_rate = parseFloat(val, 16);
                event.chorus_changed = true;
            }

            val = findJsonKey(chorusPtr, chorusLen, "depth");
            if (val) {
                event.chorus_depth = parseFloat(val, 16);
                event.chorus_changed = true;
            }

            val = findJsonKey(chorusPtr, chorusLen, "wet_mix");
            if (val) {
                event.chorus_wet = parseFloat(val, 16);
                event.chorus_changed = true;
            }
        }
    }

    // Parse master_volume
    const char* volPtr = findJsonKey(effectsPtr, effectsLen, "master_volume");
    if (volPtr && *volPtr == '{') {
        const char* volEnd = findClosingBracket(volPtr, effectsEnd, '{', '}');
        if (volEnd) {
            size_t volLen = volEnd - volPtr;
            const char* val = findJsonKey(volPtr, volLen, "level");
            if (val) {
                event.master_volume = parseFloat(val, 16);
                event.master_volume_changed = true;
            }
        }
    }

    return true;
}

// ============================================
// Effect Application (SKELETON - Serial debug)
// ============================================

void FXEngine::applyEvent(const FXEvent& event) {
    Serial.print("[FXEngine] Applying event at ");
    Serial.print(event.time_ms);
    Serial.println("ms");

    if (event.reverb_changed) {
        applyReverb(event);
    }

    if (event.delay_changed) {
        applyDelay(event);
    }

    if (event.chorus_changed) {
        applyChorus(event);
    }

    if (event.eq_changed) {
        applyEQ(event);
    }

    if (event.master_volume_changed) {
        applyMasterVolume(event);
    }

    // Update cumulative state
    if (event.reverb_changed) {
        currentState_.reverb_enabled = event.reverb_enabled;
        if (!isnan(event.reverb_room_size)) currentState_.reverb_room_size = event.reverb_room_size;
        if (!isnan(event.reverb_damping)) currentState_.reverb_damping = event.reverb_damping;
        if (!isnan(event.reverb_wet)) currentState_.reverb_wet = event.reverb_wet;
    }

    if (event.delay_changed) {
        currentState_.delay_enabled = event.delay_enabled;
        if (!isnan(event.delay_time_ms)) currentState_.delay_time_ms = event.delay_time_ms;
        if (!isnan(event.delay_feedback)) currentState_.delay_feedback = event.delay_feedback;
        if (!isnan(event.delay_wet)) currentState_.delay_wet = event.delay_wet;
    }

    if (event.chorus_changed) {
        currentState_.chorus_enabled = event.chorus_enabled;
        if (!isnan(event.chorus_rate)) currentState_.chorus_rate = event.chorus_rate;
        if (!isnan(event.chorus_depth)) currentState_.chorus_depth = event.chorus_depth;
        if (!isnan(event.chorus_wet)) currentState_.chorus_wet = event.chorus_wet;
    }

    if (event.master_volume_changed && !isnan(event.master_volume)) {
        currentState_.master_volume = event.master_volume;
    }
}

void FXEngine::applyReverb(const FXEvent& event) {
    Serial.print("  [TODO] Reverb: enabled=");
    Serial.print(event.reverb_enabled ? "true" : "false");

    if (!isnan(event.reverb_room_size)) {
        Serial.print(", room_size=");
        Serial.print(event.reverb_room_size);
    }
    if (!isnan(event.reverb_damping)) {
        Serial.print(", damping=");
        Serial.print(event.reverb_damping);
    }
    if (!isnan(event.reverb_wet)) {
        Serial.print(", wet=");
        Serial.print(event.reverb_wet);
    }
    Serial.println();

    // TODO: Apply to AudioEffectFreeverb when available
    // if (reverbLeft_ && reverbRight_) {
    //     if (!isnan(event.reverb_room_size)) reverbLeft_->roomsize(event.reverb_room_size);
    //     if (!isnan(event.reverb_damping)) reverbLeft_->damping(event.reverb_damping);
    //     // wet_mix would go to mixer gain
    // }
}

void FXEngine::applyDelay(const FXEvent& event) {
    Serial.print("  [TODO] Delay: enabled=");
    Serial.print(event.delay_enabled ? "true" : "false");

    if (!isnan(event.delay_time_ms)) {
        Serial.print(", time=");
        Serial.print(event.delay_time_ms);
        Serial.print("ms");
    }
    if (!isnan(event.delay_feedback)) {
        Serial.print(", feedback=");
        Serial.print(event.delay_feedback);
    }
    if (!isnan(event.delay_wet)) {
        Serial.print(", wet=");
        Serial.print(event.delay_wet);
    }
    Serial.println();

    // TODO: Implement AudioEffectDelay
}

void FXEngine::applyChorus(const FXEvent& event) {
    Serial.print("  [TODO] Chorus: enabled=");
    Serial.print(event.chorus_enabled ? "true" : "false");

    if (!isnan(event.chorus_rate)) {
        Serial.print(", rate=");
        Serial.print(event.chorus_rate);
        Serial.print("Hz");
    }
    if (!isnan(event.chorus_depth)) {
        Serial.print(", depth=");
        Serial.print(event.chorus_depth);
    }
    if (!isnan(event.chorus_wet)) {
        Serial.print(", wet=");
        Serial.print(event.chorus_wet);
    }
    Serial.println();

    // TODO: Implement AudioEffectChorus
}

void FXEngine::applyEQ(const FXEvent& event) {
    Serial.print("  [TODO] EQ:");

    if (!isnan(event.eq_low_gain)) {
        Serial.print(" low=");
        Serial.print(event.eq_low_gain);
        Serial.print("dB");
    }
    if (!isnan(event.eq_mid_gain)) {
        Serial.print(" mid=");
        Serial.print(event.eq_mid_gain);
        Serial.print("dB");
        if (!isnan(event.eq_mid_freq)) {
            Serial.print("@");
            Serial.print(event.eq_mid_freq);
            Serial.print("Hz");
        }
    }
    if (!isnan(event.eq_high_gain)) {
        Serial.print(" high=");
        Serial.print(event.eq_high_gain);
        Serial.print("dB");
    }
    Serial.println();

    // TODO: Implement AudioFilterBiquad chains for EQ
}

void FXEngine::applyMasterVolume(const FXEvent& event) {
    Serial.print("  [TODO] Master Volume: ");
    Serial.println(event.master_volume);

    // TODO: Apply to fade mixer or audio shield volume
    // fadeMixerLeft_->gain(0, event.master_volume);
    // fadeMixerRight_->gain(0, event.master_volume);
}
