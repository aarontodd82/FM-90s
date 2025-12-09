#ifndef AUDIO_STREAM_SPC_H
#define AUDIO_STREAM_SPC_H

#include <Audio.h>
#include <stdint.h>

// Forward declaration
class SPCPlayer;

// Audio stream class for Teensy Audio Library integration
// This class MUST be in its own translation unit to avoid ODR violations
// with the Audio Library's update list registration
class AudioStreamSPC : public AudioStream {
public:
    AudioStreamSPC(SPCPlayer* player = nullptr);
    virtual ~AudioStreamSPC() = default;

    void update() override;

    // Set the player pointer (for shared AudioStreamSPC pattern)
    void setPlayer(SPCPlayer* player);

    // Debug methods to check if update is being called
    uint32_t getUpdateCount() const { return updateCount_; }
    volatile uint32_t getTicks() const { return ticks_; }

    // Prevent copies (critical for Audio Library registration)
    AudioStreamSPC(const AudioStreamSPC&) = delete;
    AudioStreamSPC& operator=(const AudioStreamSPC&) = delete;

private:
    SPCPlayer* player_;
    bool firstUpdate_;
    uint32_t updateCount_;
    volatile uint32_t ticks_;
};

#endif // AUDIO_STREAM_SPC_H