#ifndef AUDIO_CONNECTION_MANAGER_H
#define AUDIO_CONNECTION_MANAGER_H

#include <Arduino.h>
#include <Audio.h>
#include <vector>

/**
 * AudioConnectionManager - Thread-Safe Connection Lifecycle Management
 *
 * Purpose:
 * - Centralize AudioConnection creation/deletion
 * - Prevent race conditions with Teensy Audio Library ISR
 * - Guarantee safe cleanup sequence
 *
 * Problem it solves:
 * Teensy Audio Library runs AudioStream::update_all() in an ISR.
 * Deleting AudioConnections while ISR is active causes use-after-free crashes.
 *
 * Solution:
 * 1. Mute audio first
 * 2. delay(10) to ensure ISR cycle completes
 * 3. Delete connections
 * 4. delay(5) to ensure deletion completes
 *
 * Usage:
 *   AudioConnectionManager connMgr;
 *
 *   // Create connection
 *   AudioConnection* conn = connMgr.connect(source, srcPort, dest, destPort);
 *
 *   // When stopping player
 *   connMgr.muteAndDisconnect(fadeMixerLeft, fadeMixerRight);
 *
 *   // When destroying player
 *   connMgr.disconnectAll();
 */
class AudioConnectionManager {
public:
    AudioConnectionManager() = default;
    ~AudioConnectionManager();

    // ============================================
    // CONNECTION CREATION
    // ============================================

    /**
     * Create a new audio connection
     *
     * @param source Source audio stream
     * @param sourcePort Output port on source (usually 0 for mono, 0/1 for stereo)
     * @param dest Destination audio stream (usually a mixer)
     * @param destPort Input port on dest (mixer channel)
     * @return Pointer to created connection (owned by manager)
     *
     * NOTE: Connection is tracked internally and will be auto-deleted on disconnectAll()
     */
    AudioConnection* connect(AudioStream& source, uint8_t sourcePort,
                            AudioStream& dest, uint8_t destPort);

    /**
     * Create stereo pair of connections
     *
     * @param source Source audio stream (must have 2 outputs)
     * @param destLeft Left channel destination (usually mixerLeft)
     * @param destRight Right channel destination (usually mixerRight)
     * @param destChannel Mixer channel to use (0-3)
     * @return true if both connections created successfully
     *
     * Common usage:
     *   connectStereo(*spcPlayer, mixerLeft, mixerRight, 1);
     */
    bool connectStereo(AudioStream& source,
                      AudioMixer4& destLeft, AudioMixer4& destRight,
                      uint8_t destChannel);

    // ============================================
    // SAFE DISCONNECTION
    // ============================================

    /**
     * Mute audio and disconnect all connections (SAFE)
     *
     * This is the primary cleanup method - USE THIS when stopping players!
     *
     * @param fadeMixerLeft Left fade mixer to mute
     * @param fadeMixerRight Right fade mixer to mute
     *
     * Sequence:
     * 1. Mute fade mixers (immediate silence)
     * 2. delay(10 ms) - ensure audio ISR completes current cycle
     * 3. Delete all AudioConnections
     * 4. delay(5 ms) - ensure deletions complete
     * 5. Clear connection list
     *
     * Thread safety:
     * - Audio ISR cannot access deleted connections due to delays
     * - Muting ensures no pops/clicks during disconnection
     */
    void muteAndDisconnect(AudioMixer4& fadeMixerLeft, AudioMixer4& fadeMixerRight);

    /**
     * Disconnect all connections without muting
     *
     * USE WITH CAUTION - Only call when audio is already muted!
     *
     * Typical usage: Player destructor after stop() already called
     */
    void disconnectAll();

    /**
     * Disconnect a specific connection
     *
     * @param conn Connection to disconnect
     *
     * NOTE: Uses safety delay before deletion
     */
    void disconnect(AudioConnection* conn);

    // ============================================
    // QUERY
    // ============================================

    /**
     * Get number of active connections
     * @return Count of connections managed by this instance
     */
    size_t getConnectionCount() const {
        return connections_.size();
    }

    /**
     * Check if any connections exist
     * @return true if connections list is not empty
     */
    bool hasConnections() const {
        return !connections_.empty();
    }

private:
    std::vector<AudioConnection*> connections_;

    // Safety delay constants
    static constexpr uint32_t AUDIO_ISR_SAFETY_DELAY_MS = 10;  // Time for ISR to complete
    static constexpr uint32_t DELETION_SAFETY_DELAY_MS = 5;    // Time for deletion to complete
};

#endif // AUDIO_CONNECTION_MANAGER_H
