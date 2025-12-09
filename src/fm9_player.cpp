#include "fm9_player.h"
#include "audio_stream_fm9_wav.h"
#include "audio_stream_fm9_mp3.h"
#include <string.h>

// External WAV stream (defined in main.cpp)
extern AudioStreamFM9Wav* g_fm9WavStream;

// External MP3 stream (defined in main.cpp)
extern AudioStreamFM9Mp3* g_fm9Mp3Stream;

FM9Player::FM9Player(const PlayerConfig& config)
    : config_(config)
    , fileSource_(config.fileSource)
    , coverImage_(nullptr)
    , vgmPlayer_(nullptr)
    , audioPlaying_(false)
    , fm9AudioMixerLeft_(config.fm9AudioMixerLeft)
    , fm9AudioMixerRight_(config.fm9AudioMixerRight)
    , dacNesMixerLeft_(config.dacNesMixerLeft)
    , dacNesMixerRight_(config.dacNesMixerRight)
    , audioGain_(0.6f)
    , completionCallback_(nullptr)
    , completionFired_(false) {
    memset(currentFileName_, 0, sizeof(currentFileName_));

    // Ensure FM9 audio channels start muted
    // FM9 audio pre-mixer: WAV on ch0, MP3 on ch1
    if (fm9AudioMixerLeft_) {
        fm9AudioMixerLeft_->gain(FM9_WAV_CHANNEL, 0.0f);
        fm9AudioMixerLeft_->gain(FM9_MP3_CHANNEL, 0.0f);
    }
    if (fm9AudioMixerRight_) {
        fm9AudioMixerRight_->gain(FM9_WAV_CHANNEL, 0.0f);
        fm9AudioMixerRight_->gain(FM9_MP3_CHANNEL, 0.0f);
    }
    // DAC/NES mixer: FM9 audio output on ch3
    if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 0.0f);
    if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 0.0f);

    Serial.println("[FM9Player] Created");
}

FM9Player::~FM9Player() {
    stop();

    freeCoverImage();

    if (vgmPlayer_) {
        delete vgmPlayer_;
        vgmPlayer_ = nullptr;
    }

    Serial.println("[FM9Player] Destroyed");
}

bool FM9Player::loadFile(const char* filename) {
    Serial.print("[FM9Player] Loading: ");
    Serial.println(filename);

    // Stop any current playback
    stop();

    // Clear previous state
    fm9File_.clear();
    fxEngine_.clear();
    freeCoverImage();

    if (vgmPlayer_) {
        delete vgmPlayer_;
        vgmPlayer_ = nullptr;
    }

    // Extract filename for display
    const char* lastSlash = strrchr(filename, '/');
    if (lastSlash) {
        strncpy(currentFileName_, lastSlash + 1, sizeof(currentFileName_) - 1);
    } else {
        strncpy(currentFileName_, filename, sizeof(currentFileName_) - 1);
    }

    // Load FM9 file (parses extensions, extracts audio)
    if (!fm9File_.loadFromFile(filename, fileSource_)) {
        Serial.println("[FM9Player] Failed to load FM9 file");
        return false;
    }

    // Create VGMPlayer for the VGM portion
    vgmPlayer_ = new VGMPlayer(config_);
    if (!vgmPlayer_) {
        Serial.println("[FM9Player] Failed to create VGMPlayer");
        return false;
    }

    // Load the VGM portion
    // FM9 files are essentially VGZ files, so VGMPlayer can load them directly
    // The FM9 extensions (after 0x66) are ignored by VGMPlayer
    if (!vgmPlayer_->loadFile(filename)) {
        Serial.println("[FM9Player] VGMPlayer failed to load VGM portion");
        delete vgmPlayer_;
        vgmPlayer_ = nullptr;
        return false;
    }

    // Load FX automation if present
    if (fm9File_.hasFX()) {
        const char* fxJson = fm9File_.getFXJson();
        size_t fxSize = fm9File_.getFXJsonSize();
        if (fxJson && fxSize > 0) {
            fxEngine_.loadFromJson(fxJson, fxSize);
        }
    }

    // Load audio directly from FM9 file (no temp file extraction needed)
    if (fm9File_.hasAudio()) {
        uint8_t audioFormat = fm9File_.getAudioFormat();

        if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream) {
            // Stream WAV directly from the FM9 file at the audio offset
            if (g_fm9WavStream->loadFromOffset(filename,
                                                fm9File_.getAudioOffset(),
                                                fm9File_.getAudioSize())) {
                Serial.print("[FM9Player] WAV loaded directly: ");
                Serial.print(g_fm9WavStream->getTotalSamples());
                Serial.print(" samples, ");
                Serial.print(g_fm9WavStream->getDurationMs());
                Serial.println(" ms");
            } else {
                Serial.println("[FM9Player] WARNING: Failed to load WAV from offset");
            }
        } else if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream) {
            // Stream MP3 directly from the FM9 file at the audio offset
            if (g_fm9Mp3Stream->loadFromOffset(filename,
                                                fm9File_.getAudioOffset(),
                                                fm9File_.getAudioSize())) {
                Serial.print("[FM9Player] MP3 loaded directly: ~");
                Serial.print(g_fm9Mp3Stream->getTotalSamples());
                Serial.print(" samples, ");
                Serial.print(g_fm9Mp3Stream->getDurationMs());
                Serial.println(" ms");
            } else {
                Serial.println("[FM9Player] WARNING: Failed to load MP3 from offset");
            }
        }
    }

    // Load cover image if present
    if (fm9File_.hasImage()) {
        if (loadCoverImage(filename)) {
            Serial.println("[FM9Player] Cover image loaded");
        } else {
            Serial.println("[FM9Player] WARNING: Failed to load cover image");
        }
    }

    Serial.println("[FM9Player] Load complete");
    return true;
}

void FM9Player::play() {
    if (!vgmPlayer_) {
        Serial.println("[FM9Player] No VGM loaded");
        return;
    }

    Serial.println("[FM9Player] Starting playback");

    // Reset state
    fxEngine_.reset();
    completionFired_ = false;

    // PRE-FILL audio buffer BEFORE VGM clock starts
    // This avoids initial sync offset from SD read delays
    uint8_t audioFormat = fm9File_.getAudioFormat();

    if (fm9File_.hasAudio() && audioFormat == FM9_AUDIO_WAV &&
        g_fm9WavStream && g_fm9WavStream->isLoaded()) {
        Serial.println("[FM9Player] Pre-filling WAV buffer...");
        g_fm9WavStream->play();  // Resets state and pre-fills buffer (takes 5-20ms)
    } else if (fm9File_.hasAudio() && audioFormat == FM9_AUDIO_MP3 &&
               g_fm9Mp3Stream && g_fm9Mp3Stream->isLoaded()) {
        Serial.println("[FM9Player] Pre-filling MP3 buffer...");
        g_fm9Mp3Stream->play();  // Resets state and pre-fills buffer
    }

    // NOW start VGM playback (clock starts here with sampleCount_ = 0)
    vgmPlayer_->play();

    // Unmute audio channels (fast - just sets mixer gains)
    // Two-level mixer architecture:
    // 1. fm9AudioMixer: unmute the specific format (WAV or MP3)
    // 2. dacNesMixer: unmute FM9 output channel
    if (fm9File_.hasAudio()) {
        if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream && g_fm9WavStream->isLoaded()) {
            // Unmute WAV on fm9AudioMixer, mute MP3
            if (fm9AudioMixerLeft_) {
                fm9AudioMixerLeft_->gain(FM9_WAV_CHANNEL, audioGain_);
                fm9AudioMixerLeft_->gain(FM9_MP3_CHANNEL, 0.0f);
            }
            if (fm9AudioMixerRight_) {
                fm9AudioMixerRight_->gain(FM9_WAV_CHANNEL, audioGain_);
                fm9AudioMixerRight_->gain(FM9_MP3_CHANNEL, 0.0f);
            }
            // Unmute FM9 output on dacNesMixer
            if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 1.0f);
            if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 1.0f);
            audioPlaying_ = true;
            Serial.println("[FM9Player] WAV playback started (synced with VGM)");
        } else if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream && g_fm9Mp3Stream->isLoaded()) {
            // Unmute MP3 on fm9AudioMixer, mute WAV
            if (fm9AudioMixerLeft_) {
                fm9AudioMixerLeft_->gain(FM9_WAV_CHANNEL, 0.0f);
                fm9AudioMixerLeft_->gain(FM9_MP3_CHANNEL, audioGain_);
            }
            if (fm9AudioMixerRight_) {
                fm9AudioMixerRight_->gain(FM9_WAV_CHANNEL, 0.0f);
                fm9AudioMixerRight_->gain(FM9_MP3_CHANNEL, audioGain_);
            }
            // Unmute FM9 output on dacNesMixer
            if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 1.0f);
            if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 1.0f);
            audioPlaying_ = true;
            Serial.println("[FM9Player] MP3 playback started (synced with VGM)");
        }
    }
}

void FM9Player::pause() {
    if (vgmPlayer_) {
        vgmPlayer_->pause();
    }
    pauseAudioPlayback();
}

void FM9Player::resume() {
    if (vgmPlayer_) {
        vgmPlayer_->resume();
    }
    resumeAudioPlayback();
}

void FM9Player::stop() {
    Serial.println("[FM9Player] Stopping");

    // Stop audio first
    stopAudioPlayback();

    // Stop VGM
    if (vgmPlayer_) {
        vgmPlayer_->stop();
    }

    // Reset FX engine
    fxEngine_.reset();
}

void FM9Player::update() {
    if (!vgmPlayer_) return;

    // Update VGM playback
    vgmPlayer_->update();

    // === SYNCHRONIZE AUDIO STREAM ===
    // Keep audio stream aligned with VGM sample position
    // Same pattern as DAC pre-render sync in VGMPlayer
    if (audioPlaying_) {
        uint32_t vgmSamplePos = vgmPlayer_->getCurrentSample();
        uint8_t audioFormat = fm9File_.getAudioFormat();

        if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream && g_fm9WavStream->isPlaying()) {
            // Tell WAV stream what sample it should be at
            g_fm9WavStream->setTargetSample(vgmSamplePos);

            // Refill WAV buffer from SD (non-blocking, only if needed)
            if (g_fm9WavStream->needsRefill()) {
                g_fm9WavStream->refillBuffer();
            }
        } else if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream && g_fm9Mp3Stream->isPlaying()) {
            // Tell MP3 stream what sample it should be at
            g_fm9Mp3Stream->setTargetSample(vgmSamplePos);

            // Refill MP3 buffer (decodes frames, non-blocking, only if needed)
            if (g_fm9Mp3Stream->needsRefill()) {
                g_fm9Mp3Stream->refillBuffer();
            }
        }
    }

    // Update FX engine with current position
    if (fxEngine_.hasEvents()) {
        uint32_t posMs = getPositionMs();
        fxEngine_.update(posMs);
    }

    // Check for completion (only fire once)
    // VGM finishes first (or they finish together), WAV may still be playing
    // IMPORTANT: Only check if we were actually playing - isStopped() is true for
    // newly loaded files that haven't started yet!
    if (vgmPlayer_->isPlaying() == false &&
        vgmPlayer_->getState() == PlayerState::STOPPED &&
        !completionFired_ &&
        audioPlaying_) {  // Only if we were actually playing audio
        completionFired_ = true;

        // Stop audio too
        stopAudioPlayback();

        // Call completion callback
        if (completionCallback_) {
            completionCallback_();
        }
    }
}

void FM9Player::setCompletionCallback(CompletionCallback callback) {
    completionCallback_ = callback;
}

PlayerState FM9Player::getState() const {
    if (!vgmPlayer_) return PlayerState::IDLE;
    return vgmPlayer_->getState();
}

bool FM9Player::isPlaying() const {
    return vgmPlayer_ && vgmPlayer_->isPlaying();
}

bool FM9Player::isPaused() const {
    return vgmPlayer_ && vgmPlayer_->isPaused();
}

bool FM9Player::isStopped() const {
    return !vgmPlayer_ || vgmPlayer_->isStopped();
}

uint32_t FM9Player::getDurationMs() const {
    if (!vgmPlayer_) return 0;
    return vgmPlayer_->getDurationMs();
}

uint32_t FM9Player::getPositionMs() const {
    if (!vgmPlayer_) return 0;
    return vgmPlayer_->getPositionMs();
}

float FM9Player::getProgress() const {
    if (!vgmPlayer_) return 0.0f;
    return vgmPlayer_->getProgress();
}

bool FM9Player::isLooping() const {
    if (!vgmPlayer_) return false;
    return vgmPlayer_->isLooping();
}

void FM9Player::printStats() const {
    Serial.println("[FM9Player] Stats:");
    Serial.print("  Has audio: ");
    Serial.println(fm9File_.hasAudio() ? "yes" : "no");
    Serial.print("  Audio format: ");
    uint8_t fmt = fm9File_.getAudioFormat();
    Serial.println(fmt == FM9_AUDIO_WAV ? "WAV" : (fmt == FM9_AUDIO_MP3 ? "MP3" : "none"));
    Serial.print("  Has FX: ");
    Serial.println(fm9File_.hasFX() ? "yes" : "no");
    Serial.print("  FX events: ");
    Serial.println(fxEngine_.getEventCount());
    Serial.print("  Audio playing: ");
    Serial.println(audioPlaying_ ? "yes" : "no");

    if (audioPlaying_) {
        if (fmt == FM9_AUDIO_WAV && g_fm9WavStream) {
            Serial.print("  WAV buffer level: ");
            Serial.print(g_fm9WavStream->getBufferLevel());
            Serial.println(" samples");
            Serial.print("  WAV sync drift: ");
            Serial.print(g_fm9WavStream->getSyncDrift());
            Serial.print(" samples (mode: ");
            int8_t mode = g_fm9WavStream->getSyncMode();
            Serial.print(mode == 0 ? "normal" : (mode > 0 ? "speedup" : "slowdown"));
            Serial.println(")");
            Serial.print("  WAV underruns: ");
            Serial.println(g_fm9WavStream->getUnderruns());
        } else if (fmt == FM9_AUDIO_MP3 && g_fm9Mp3Stream) {
            Serial.print("  MP3 buffer level: ");
            Serial.print(g_fm9Mp3Stream->getBufferLevel());
            Serial.println(" samples");
            Serial.print("  MP3 sync drift: ");
            Serial.print(g_fm9Mp3Stream->getSyncDrift());
            Serial.print(" samples (mode: ");
            int8_t mode = g_fm9Mp3Stream->getSyncMode();
            Serial.print(mode == 0 ? "normal" : (mode > 0 ? "speedup" : "slowdown"));
            Serial.println(")");
            Serial.print("  MP3 underruns: ");
            Serial.print(g_fm9Mp3Stream->getUnderruns());
            Serial.print(", decode errors: ");
            Serial.println(g_fm9Mp3Stream->getDecodeErrors());
        }
    }

    if (vgmPlayer_) {
        Serial.println("  VGM Stats:");
        vgmPlayer_->printStats();
    }
}

ChipType FM9Player::getChipType() const {
    if (!vgmPlayer_) return ChipType::NONE;
    return vgmPlayer_->getChipType();
}

// ============================================
// Audio Playback Management
// ============================================

void FM9Player::startAudioPlayback() {
    // NOTE: This method is legacy and not called from play()
    // The play() method handles audio startup directly now
    // Keeping for potential future use or external calls

    Serial.println("[FM9Player] startAudioPlayback() called");

    if (!fm9File_.hasAudio()) {
        Serial.println("[FM9Player] No audio to play");
        return;
    }

    uint8_t audioFormat = fm9File_.getAudioFormat();

    if (audioFormat == FM9_AUDIO_WAV) {
        if (!g_fm9WavStream || !g_fm9WavStream->isLoaded()) {
            Serial.println("[FM9Player] ERROR: WAV not loaded!");
            return;
        }
        g_fm9WavStream->play();
        if (fm9AudioMixerLeft_) fm9AudioMixerLeft_->gain(FM9_WAV_CHANNEL, audioGain_);
        if (fm9AudioMixerRight_) fm9AudioMixerRight_->gain(FM9_WAV_CHANNEL, audioGain_);
    } else if (audioFormat == FM9_AUDIO_MP3) {
        if (!g_fm9Mp3Stream || !g_fm9Mp3Stream->isLoaded()) {
            Serial.println("[FM9Player] ERROR: MP3 not loaded!");
            return;
        }
        g_fm9Mp3Stream->play();
        if (fm9AudioMixerLeft_) fm9AudioMixerLeft_->gain(FM9_MP3_CHANNEL, audioGain_);
        if (fm9AudioMixerRight_) fm9AudioMixerRight_->gain(FM9_MP3_CHANNEL, audioGain_);
    } else {
        Serial.println("[FM9Player] Unknown audio format");
        return;
    }

    // Unmute FM9 output on dacNesMixer
    if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 1.0f);
    if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 1.0f);

    audioPlaying_ = true;
    Serial.printf("[FM9Player] %s playback started\n",
                  audioFormat == FM9_AUDIO_WAV ? "WAV" : "MP3");
}

void FM9Player::stopAudioPlayback() {
    // Mute all FM9 audio channels
    if (fm9AudioMixerLeft_) {
        fm9AudioMixerLeft_->gain(FM9_WAV_CHANNEL, 0.0f);
        fm9AudioMixerLeft_->gain(FM9_MP3_CHANNEL, 0.0f);
    }
    if (fm9AudioMixerRight_) {
        fm9AudioMixerRight_->gain(FM9_WAV_CHANNEL, 0.0f);
        fm9AudioMixerRight_->gain(FM9_MP3_CHANNEL, 0.0f);
    }
    if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 0.0f);
    if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 0.0f);

    uint8_t audioFormat = fm9File_.getAudioFormat();

    // Stop and close WAV stream (release file handle)
    if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream) {
        if (g_fm9WavStream->isPlaying()) {
            g_fm9WavStream->stop();
        }
        g_fm9WavStream->closeFile();
        Serial.println("[FM9Player] WAV stream stopped and closed");
    }

    // Stop and close MP3 stream (release file handle and decoder)
    if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream) {
        if (g_fm9Mp3Stream->isPlaying()) {
            g_fm9Mp3Stream->stop();
        }
        g_fm9Mp3Stream->closeFile();
        Serial.println("[FM9Player] MP3 stream stopped and closed");
    }

    audioPlaying_ = false;
}

void FM9Player::pauseAudioPlayback() {
    uint8_t audioFormat = fm9File_.getAudioFormat();

    if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream) {
        g_fm9WavStream->pause();
    } else if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream) {
        g_fm9Mp3Stream->pause();
    }

    // Also mute the output
    if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 0.0f);
    if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 0.0f);
}

void FM9Player::resumeAudioPlayback() {
    if (!audioPlaying_) return;

    uint8_t audioFormat = fm9File_.getAudioFormat();

    if (audioFormat == FM9_AUDIO_WAV && g_fm9WavStream) {
        g_fm9WavStream->resume();
    } else if (audioFormat == FM9_AUDIO_MP3 && g_fm9Mp3Stream) {
        g_fm9Mp3Stream->resume();
    }

    // Unmute the output
    if (dacNesMixerLeft_) dacNesMixerLeft_->gain(FM9_DAC_CHANNEL, 1.0f);
    if (dacNesMixerRight_) dacNesMixerRight_->gain(FM9_DAC_CHANNEL, 1.0f);
}

void FM9Player::updateFXEngine() {
    // TODO: Implement FX automation
    // This will process FX events at the current playback position
    // and apply effects (reverb, EQ, etc.) to the audio output
}

// ============================================
// Cover Image Loading
// ============================================

bool FM9Player::loadCoverImage(const char* filename) {
    if (!fileSource_) return false;

    // Allocate buffer for cover image (20KB)
    // Try PSRAM first (extmem_malloc), fall back to regular malloc
    #if defined(__IMXRT1062__)  // Teensy 4.x
    coverImage_ = (uint16_t*)extmem_malloc(FM9_IMAGE_SIZE);
    if (!coverImage_) {
        Serial.println("[FM9Player] PSRAM allocation failed, trying regular RAM");
        coverImage_ = (uint16_t*)malloc(FM9_IMAGE_SIZE);
    }
    #else
    coverImage_ = (uint16_t*)malloc(FM9_IMAGE_SIZE);
    #endif

    if (!coverImage_) {
        Serial.println("[FM9Player] Failed to allocate cover image buffer");
        return false;
    }

    // Open file and seek to image offset
    File file = fileSource_->open(filename, FILE_READ);
    if (!file) {
        Serial.println("[FM9Player] Failed to open file for cover image");
        freeCoverImage();
        return false;
    }

    uint32_t imageOffset = fm9File_.getImageOffset();
    if (!file.seek(imageOffset)) {
        Serial.print("[FM9Player] Failed to seek to image offset: ");
        Serial.println(imageOffset);
        file.close();
        freeCoverImage();
        return false;
    }

    // Read the entire image (20000 bytes)
    size_t bytesRead = file.read((uint8_t*)coverImage_, FM9_IMAGE_SIZE);
    file.close();

    if (bytesRead != FM9_IMAGE_SIZE) {
        Serial.print("[FM9Player] Short read for cover image: ");
        Serial.print(bytesRead);
        Serial.print(" / ");
        Serial.println(FM9_IMAGE_SIZE);
        freeCoverImage();
        return false;
    }

    return true;
}

void FM9Player::freeCoverImage() {
    if (coverImage_) {
        #if defined(__IMXRT1062__)  // Teensy 4.x
        // Check if it's in PSRAM or regular RAM
        // PSRAM addresses are >= 0x70000000
        if ((uint32_t)coverImage_ >= 0x70000000) {
            extmem_free(coverImage_);
        } else {
            free(coverImage_);
        }
        #else
        free(coverImage_);
        #endif
        coverImage_ = nullptr;
    }
}
