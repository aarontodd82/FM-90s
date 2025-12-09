#include "player_manager.h"
#include "audio_system.h"
#include "midi_player.h"
#include "vgm_player.h"
#include "fm9_player.h"
#include "spc_player.h"
#include "ui/framework/event_manager.h"
#include "ui/screen_manager.h"
#include <string.h>

// ========================================
// Constructor / Destructor
// ========================================

PlayerManager::PlayerManager(const PlayerConfig& config)
    : config_(config)
    , currentPlayer_(nullptr)
    , currentFormat_(FileFormat::UNKNOWN)
    , opl3_(config.opl3)
    , fileSource_(config.fileSource)
    , drumSampler_(config.drumSampler)
    , mixerLeft_(config.mixerLeft)
    , mixerRight_(config.mixerRight)
    , submixerLeft_(config.mixerChannel1Left)
    , submixerRight_(config.mixerChannel1Right)
    , dacNesMixerLeft_(config.dacNesMixerLeft)
    , dacNesMixerRight_(config.dacNesMixerRight)
    , finalMixerLeft_(config.finalMixerLeft)
    , finalMixerRight_(config.finalMixerRight)
    , fadeMixerLeft_(config.fadeMixerLeft)
    , fadeMixerRight_(config.fadeMixerRight)
    , reverbLeft_(config.reverbLeft)
    , reverbRight_(config.reverbRight)
    , crossfeedEnabled_(config.crossfeedEnabled)
    , reverbEnabled_(config.reverbEnabled)
    , playbackState_(PlaybackState::getInstance())
    , eventManager_(nullptr)
    , loadCompleteCallback_(nullptr)
    , startCompleteCallback_(nullptr)
    , stopCompleteCallback_(nullptr)
    , naturalCompletionCallback_(nullptr)
    , pendingFormat_(FileFormat::UNKNOWN)
{
    // // Serial.println("[PlayerManager] Created");
}

PlayerManager::~PlayerManager() {
    // // Serial.println("[PlayerManager] Destroying");

    // Phase 2: stopAndDestroy() deleted, so do cleanup directly
    if (currentPlayer_) {
        centralizedStop();
        delay(50);  // Safety delay after stop
        destroyCurrentPlayer();
        delay(50);  // Safety delay after destroy
    }
}

// ========================================
// Primary API (Phase 2: Callback-Driven)
// ========================================

void PlayerManager::prepareFileAsync(const char* path, LoadCompleteCallback callback) {
    loadCompleteCallback_ = callback;

    if (!path) {
        // // Serial.println("[PlayerManager] ERROR: Null path");
        if (loadCompleteCallback_) loadCompleteCallback_(false);
        return;
    }

    // // Serial.printf("[PlayerManager] prepareFileAsync: %s\n", path);

    // Store pending operation info
    pendingFilePath_ = path;
    pendingFormat_ = detectFormat(path);

    if (pendingFormat_ == FileFormat::UNKNOWN) {
        // // Serial.println("[PlayerManager] ERROR: Unknown format");
        if (loadCompleteCallback_) loadCompleteCallback_(false);
        return;
    }

    // If switching formats, destroy old player
    if (currentPlayer_ && currentFormat_ != pendingFormat_) {
        // // Serial.printf("[PlayerManager] Format switch: %d -> %d, destroying old player\n",
        //              (int)currentFormat_, (int)pendingFormat_);

        centralizedStop();
        delay(50);  // Safety after stop
        destroyCurrentPlayer();
        delay(50);  // Safety after destroy

        // // Serial.println("[PlayerManager] Old player destroyed, delays complete");
    }

    // Create player on-demand if needed
    if (!currentPlayer_) {
        currentPlayer_ = createPlayer(pendingFormat_);
        if (!currentPlayer_) {
            // // Serial.println("[PlayerManager] ERROR: Player creation failed");
            if (loadCompleteCallback_) loadCompleteCallback_(false);
            return;
        }
        currentFormat_ = pendingFormat_;
    } else {
        // Reusing existing player - stop it first
        // // Serial.println("[PlayerManager] Reusing existing player, stopping first");
        centralizedStop();
        delay(50);  // Safety after stop
        // // Serial.println("[PlayerManager] Reuse stop complete");
    }

    // Load file
    bool loadSuccess = currentPlayer_->loadFile(path);

    if (!loadSuccess) {
        // // Serial.println("[PlayerManager] ERROR: loadFile failed");
        if (loadCompleteCallback_) loadCompleteCallback_(false);
        return;
    }

    // Wait for hardware to settle after load
    delay(50);
    // // Serial.println("[PlayerManager] File loaded, hardware settled");

    // Success! File is loaded and ready to play
    if (loadCompleteCallback_) loadCompleteCallback_(true);
}

void PlayerManager::startPlaybackAsync(StartCompleteCallback callback) {
    startCompleteCallback_ = callback;

    if (!currentPlayer_) {
        // // Serial.println("[PlayerManager] ERROR: No player to start");
        if (startCompleteCallback_) startCompleteCallback_(false);
        return;
    }

    // // Serial.println("[PlayerManager] startPlaybackAsync");

    // Set completion callback on player (event-driven, not polling!)
    currentPlayer_->setCompletionCallback([this]() {
        if (naturalCompletionCallback_) {
            naturalCompletionCallback_();
        }
    });

    // Centralized play - handles unmute + format-specific effects
    centralizedPlay();

    // Small delay to let audio unmute propagate
    delay(5);

    // Synchronize PlaybackState
    playbackState_->startPlayback(pendingFilePath_.c_str(), currentFormat_,
                                   currentPlayer_->isLooping());

    // Success!
    if (startCompleteCallback_) startCompleteCallback_(true);
    // // Serial.println("[PlayerManager] Playback started");
}

void PlayerManager::stopAsync(StopCompleteCallback callback) {
    stopCompleteCallback_ = callback;

    if (!currentPlayer_) {
        // Nothing to stop
        if (stopCompleteCallback_) stopCompleteCallback_();
        return;
    }

    // // Serial.println("[PlayerManager] stopAsync");

    // Centralized stop - mutes audio, disables effects
    centralizedStop();

    // CRITICAL: Wait for all stop operations to fully complete
    // - Audio ISR cycles (10ms guarantees 3+ cycles at 344Hz)
    // - Hardware reset propagation
    // - Audio connection cleanup if any
    delay(50);

    // Update PlaybackState
    playbackState_->stopPlayback();

    // // Serial.println("[PlayerManager] Stop complete");

    // Success! Everything is stopped and safe
    if (stopCompleteCallback_) stopCompleteCallback_();
}

void PlayerManager::setNaturalCompletionCallback(NaturalCompletionCallback callback) {
    naturalCompletionCallback_ = callback;
}

void PlayerManager::update() {
    if (!currentPlayer_) return;

    // Update the player
    currentPlayer_->update();

    // Synchronize PlaybackState with player progress
    if (currentPlayer_->isPlaying()) {
        playbackState_->setDuration(currentPlayer_->getDurationMs());
        playbackState_->setPosition(currentPlayer_->getPositionMs());
        playbackState_->setLooping(currentPlayer_->isLooping());
    }

    // Note: Natural completion is now handled via callbacks set in startPlaybackAsync()
    // Players call their completion callback when they finish naturally
}

void PlayerManager::pause() {
    if (!currentPlayer_) return;
    currentPlayer_->pause();
}

void PlayerManager::resume() {
    if (!currentPlayer_) return;
    currentPlayer_->resume();
}

// ========================================
// State Queries
// ========================================

PlayerState PlayerManager::getState() const {
    return currentPlayer_ ? currentPlayer_->getState() : PlayerState::IDLE;
}

bool PlayerManager::isPlaying() const {
    return currentPlayer_ ? currentPlayer_->isPlaying() : false;
}

bool PlayerManager::isPaused() const {
    return currentPlayer_ ? currentPlayer_->isPaused() : false;
}

uint32_t PlayerManager::getDurationMs() const {
    return currentPlayer_ ? currentPlayer_->getDurationMs() : 0;
}

uint32_t PlayerManager::getPositionMs() const {
    return currentPlayer_ ? currentPlayer_->getPositionMs() : 0;
}

float PlayerManager::getProgress() const {
    return currentPlayer_ ? currentPlayer_->getProgress() : 0.0f;
}

const char* PlayerManager::getFileName() const {
    return currentPlayer_ ? currentPlayer_->getFileName() : "";
}

// ========================================
// Player Creation (On-Demand)
// ========================================

IAudioPlayer* PlayerManager::createPlayer(FileFormat format) {
    // // Serial.printf("[PlayerManager] Creating player for format %d\n", (int)format);

    IAudioPlayer* player = nullptr;

    switch (format) {
        case FileFormat::MIDI:
            player = new MidiPlayer(config_);
            // // Serial.println("[PlayerManager] Created MidiPlayer");
            break;

        case FileFormat::VGM:
            player = new VGMPlayer(config_);
            // // Serial.println("[PlayerManager] Created VGMPlayer");
            break;

        case FileFormat::FM9:
            player = new FM9Player(config_);
            // // Serial.println("[PlayerManager] Created FM9Player");
            break;

        case FileFormat::SPC:
            player = new SPCPlayer(config_);
            // // Serial.println("[PlayerManager] Created SPCPlayer");
            break;

        // MOD/XM/IT/S3M removed - use FM9 format instead (converts tracker files with embedded audio)

        default:
            // // Serial.printf("[PlayerManager] ERROR: Unsupported format %d\n", (int)format);
            break;
    }

    return player;
}

void PlayerManager::destroyCurrentPlayer() {
    if (!currentPlayer_) return;

    // // Serial.printf("[PlayerManager] Destroying %d player\n", (int)currentFormat_);
    delete currentPlayer_;
    currentPlayer_ = nullptr;
    currentFormat_ = FileFormat::UNKNOWN;
}

FileFormat PlayerManager::detectFormat(const char* path) const {
    if (!path) return FileFormat::UNKNOWN;

    // Find extension (last dot)
    const char* ext = strrchr(path, '.');
    if (!ext) return FileFormat::UNKNOWN;

    ext++; // Skip the dot

    // Case-insensitive comparison
    if (strcasecmp(ext, "mid") == 0 || strcasecmp(ext, "midi") == 0 ||
        strcasecmp(ext, "smf") == 0 || strcasecmp(ext, "kar") == 0) {
        return FileFormat::MIDI;
    }

    if (strcasecmp(ext, "vgm") == 0 || strcasecmp(ext, "vgz") == 0) {
        return FileFormat::VGM;
    }

    if (strcasecmp(ext, "fm9") == 0) {
        return FileFormat::FM9;
    }

    if (strcasecmp(ext, "spc") == 0) {
        return FileFormat::SPC;
    }

    if (strcasecmp(ext, "mod") == 0) {
        return FileFormat::MOD;
    }

    if (strcasecmp(ext, "s3m") == 0) {
        return FileFormat::S3M;
    }

    if (strcasecmp(ext, "xm") == 0) {
        return FileFormat::XM;
    }

    if (strcasecmp(ext, "it") == 0) {
        return FileFormat::IT;
    }

    return FileFormat::UNKNOWN;
}

// ========================================
// Centralized Lifecycle Operations
// ========================================

void PlayerManager::centralizedPlay() {
    if (!currentPlayer_) return;

    // STEP 1: Enable format-specific effects BEFORE playing
    applyFormatSpecificEffects(currentFormat_, true);

    // STEP 2: UNMUTE audio BEFORE calling play (player expects audio ready)
    // NOTE: Currently players also call setFadeGain(1.0) - this is DUPLICATION
    // TODO Phase 4.1: Remove setFadeGain from all player play() methods
    // TODO Phase 4.1: Remove enableCrossfeed/enableReverb from MidiPlayer play()
    AudioSystem::setFadeGain(*fadeMixerLeft_, *fadeMixerRight_, 1.0f);

    // STEP 3: Call player's play() method (player handles its own logic)
    currentPlayer_->play();
}

void PlayerManager::centralizedStop() {
    if (!currentPlayer_) return;

    // STEP 1: Call player's stop() method
    currentPlayer_->stop();

    // STEP 2: MUTE audio (centralized)
    AudioSystem::setFadeGain(*fadeMixerLeft_, *fadeMixerRight_, 0.0f);

    // STEP 3: Mute line-in (hardware synthesizers)
    // Safe for all formats - software emulators don't use line-in
    // Prevents hardware noise when switching between players
    AudioSystem::muteLineIn(*mixerLeft_, *mixerRight_);

    // STEP 4: Defensively mute ALL emulator audio channels
    // Prevents hung notes from any emulator (DAC/NES/SPC/GB/MOD)
    //
    // Architecture:
    //   dacNesMixer (ch0=DAC, ch1=NES) → submixer ch0 (UNITY GAIN - never mute!)
    //   SPC → submixer ch1
    //   GB APU → submixer ch2
    //   MOD → submixer ch3
    //
    // CRITICAL: Do NOT mute submixer ch0 - it's the passthrough for dacNesMixer!
    // Muting submixer ch0 would kill both DAC and NES APU audio.

    // Mute DAC/NES/S3M pre-mixer channels (individual control)
    if (dacNesMixerLeft_ && dacNesMixerRight_) {
        dacNesMixerLeft_->gain(0, 0.0f);   // DAC Prerender
        dacNesMixerLeft_->gain(1, 0.0f);   // NES APU
        dacNesMixerLeft_->gain(2, 0.0f);   // S3M PCM
        dacNesMixerRight_->gain(0, 0.0f);
        dacNesMixerRight_->gain(1, 0.0f);
        dacNesMixerRight_->gain(2, 0.0f);
    }

    // Mute submixer channels 1-3 (NOT ch0 - that's dacNesMixer passthrough!)
    if (submixerLeft_ && submixerRight_) {
        // submixerLeft_->gain(0, ...) - DO NOT TOUCH! Must stay at 1.0 for dacNesMixer
        submixerLeft_->gain(1, 0.0f);   // SPC
        submixerLeft_->gain(2, 0.0f);   // GB APU
        submixerLeft_->gain(3, 0.0f);   // MOD
        submixerRight_->gain(1, 0.0f);
        submixerRight_->gain(2, 0.0f);
        submixerRight_->gain(3, 0.0f);
    }

    // STEP 5: Disable format-specific effects
    applyFormatSpecificEffects(currentFormat_, false);
}

void PlayerManager::applyFormatSpecificEffects(FileFormat format, bool enable) {
    // Only MIDI gets crossfeed (reverb removed to save ~50KB RAM)
    if (format == FileFormat::MIDI) {
        // Apply crossfeed if user preference allows
        if (crossfeedEnabled_) {
            AudioSystem::enableCrossfeed(*mixerLeft_, *mixerRight_, enable);
        }

        // Reverb removed to save ~50KB RAM
        // if (reverbEnabled_) {
        //     AudioSystem::enableReverb(*finalMixerLeft_, *finalMixerRight_,
        //                              *reverbLeft_, *reverbRight_, enable);
        // }
    } else {
        // All other formats: ensure effects are disabled
        if (!enable) {
            AudioSystem::enableCrossfeed(*mixerLeft_, *mixerRight_, false);
            // Reverb removed to save ~50KB RAM
            // AudioSystem::enableReverb(*finalMixerLeft_, *finalMixerRight_,
            //                          *reverbLeft_, *reverbRight_, false);
        }
    }
}

// ========================================
// Optional Components (for GUI integration)
// ========================================

void PlayerManager::setEventManager(EventManager* eventManager) {
    eventManager_ = eventManager;
    // // Serial.println("[PlayerManager] EventManager set");
}

void PlayerManager::setScreenManager(ScreenManager* screenManager) {
    // Deprecated: PlayerManager no longer does screen navigation
    // Navigation is now handled by PlaybackEventHandler responding to EVENT_PLAYBACK_STOPPED
    // // Serial.println("[PlayerManager] setScreenManager() deprecated - navigation handled by events");
    (void)screenManager;  // Suppress unused parameter warning
}
