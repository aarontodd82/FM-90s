#include "audio_connection_manager.h"
#include "audio_system.h"

AudioConnectionManager::~AudioConnectionManager() {
    // Clean up all connections on destruction
    disconnectAll();
}

AudioConnection* AudioConnectionManager::connect(AudioStream& source, uint8_t sourcePort,
                                                AudioStream& dest, uint8_t destPort) {
    AudioConnection* conn = new AudioConnection(source, sourcePort, dest, destPort);
    if (conn) {
        connections_.push_back(conn);
        // Serial.printf("[AudioConnMgr] Created connection: %p (total: %d)\n",
        //              conn, connections_.size());
    } else {
        // // Serial.println("[AudioConnMgr] ERROR: Failed to create connection!");
    }
    return conn;
}

bool AudioConnectionManager::connectStereo(AudioStream& source,
                                          AudioMixer4& destLeft, AudioMixer4& destRight,
                                          uint8_t destChannel) {
    AudioConnection* left = connect(source, 0, destLeft, destChannel);
    AudioConnection* right = connect(source, 1, destRight, destChannel);

    bool success = (left != nullptr && right != nullptr);
    // Serial.printf("[AudioConnMgr] Stereo connection %s (channel %d, total: %d)\n",
    //              success ? "SUCCESS" : "FAILED", destChannel, connections_.size());
    return success;
}

void AudioConnectionManager::muteAndDisconnect(AudioMixer4& fadeMixerLeft, AudioMixer4& fadeMixerRight) {
    if (connections_.empty()) {
        // // Serial.println("[AudioConnMgr] No connections to disconnect");
        return;
    }

    // // Serial.printf("[AudioConnMgr] Mute and disconnect %d connections\n", connections_.size());

    // STEP 1: Immediate silence to prevent pops/clicks
    AudioSystem::setFadeGain(fadeMixerLeft, fadeMixerRight, 0.0f);
    // // Serial.println("[AudioConnMgr] Fade mixers muted");

    // STEP 2: Wait for audio ISR to complete current cycle
    // Audio library update runs at ~344 Hz (every 2.9 ms)
    // 10 ms guarantees at least 3 full cycles complete
    delay(AUDIO_ISR_SAFETY_DELAY_MS);
    // // Serial.println("[AudioConnMgr] Safety delay complete (audio ISR finished)");

    // STEP 3: Delete all connections
    for (AudioConnection* conn : connections_) {
        if (conn) {
            delete conn;
        }
    }
    // // Serial.printf("[AudioConnMgr] Deleted %d connections\n", connections_.size());

    // STEP 4: Wait for deletions to propagate through audio library
    delay(DELETION_SAFETY_DELAY_MS);

    // STEP 5: Clear the list
    connections_.clear();
    // // Serial.println("[AudioConnMgr] Connection list cleared");
}

void AudioConnectionManager::disconnectAll() {
    if (connections_.empty()) {
        return;
    }

    // // Serial.printf("[AudioConnMgr] Disconnecting %d connections (no mute)\n", connections_.size());

    // Wait for ISR safety (audio should already be muted by caller!)
    delay(AUDIO_ISR_SAFETY_DELAY_MS);

    // Delete all connections
    for (AudioConnection* conn : connections_) {
        if (conn) {
            delete conn;
        }
    }

    // Wait for deletions to complete
    delay(DELETION_SAFETY_DELAY_MS);

    connections_.clear();
    // // Serial.println("[AudioConnMgr] All connections disconnected");
}

void AudioConnectionManager::disconnect(AudioConnection* conn) {
    if (!conn) return;

    // Find and remove from list
    auto it = std::find(connections_.begin(), connections_.end(), conn);
    if (it != connections_.end()) {
        connections_.erase(it);

        // Safety delay before deletion
        delay(AUDIO_ISR_SAFETY_DELAY_MS);
        delete conn;
        delay(DELETION_SAFETY_DELAY_MS);

        // Serial.printf("[AudioConnMgr] Disconnected single connection: %p (remaining: %d)\n",
        //              conn, connections_.size());
    }
}
