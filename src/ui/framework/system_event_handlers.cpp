#include "system_event_handlers.h"
#include "../screen_manager.h"
#include "../../retro_ui.h"
#include "../../file_source.h"
#include "../../playback_state.h"
#include "../../dos_colors.h"
#include "../../player_manager.h"  // For unified player management
#include "../../playback_coordinator.h"  // For requestStop()
#include "../../midi_player.h"  // For MIDI-specific reset (resetTempo/resetProgramChange)
#include "../../audio_system.h"
#include "../../audio_globals.h"
#include "../../drum_sampler_v2.h"

// External global audio settings from main.cpp
extern bool g_drumSamplerEnabled;
extern bool g_crossfeedEnabled;
extern bool g_reverbEnabled;
extern DrumSamplerV2* g_drumSampler;

/**
 * SystemEventHandlers Implementation
 *
 * Handles system-level events that affect the application state:
 * - USB drive disconnection (stop playback, navigate away)
 * - Playback state changes (auto-navigation)
 */

// ============================================
// USB EVENT HANDLER
// ============================================

ScreenContext* USBEventHandler::context_ = nullptr;
ScreenManager* USBEventHandler::screenManager_ = nullptr;

void USBEventHandler::initialize(ScreenContext* context, ScreenManager* screenManager) {
    context_ = context;
    screenManager_ = screenManager;

    if (!context || !context->eventManager) {
        // // Serial.println("[USBEventHandler] ERROR: Invalid context or event manager");
        return;
    }

    // Subscribe to USB events (automatic hot-plug detection)
    context->eventManager->on(EventManager::EVENT_USB_CONNECTED, onUSBConnected, nullptr);
    context->eventManager->on(EventManager::EVENT_USB_DISCONNECTED, onUSBDisconnected, nullptr);

    // // Serial.println("[USBEventHandler] Initialized (automatic hot-plug detection enabled)");
}

void USBEventHandler::cleanup() {
    if (context_ && context_->eventManager) {
        context_->eventManager->offAll(nullptr);
    }
    context_ = nullptr;
    screenManager_ = nullptr;
}

void USBEventHandler::onUSBConnected(void* userData) {
    (void)userData;

    if (!context_ || !context_->ui) return;

    // // Serial.println("[USBEventHandler] USB drive connected");

    // Show notification (screens will update themselves via events)
    context_->ui->showStatusNotification("USB Drive connected", 3000, DOS_BLACK, DOS_GREEN);
}

void USBEventHandler::onUSBDisconnected(void* userData) {
    (void)userData;

    if (!context_ || !screenManager_) return;

    // // Serial.println("[USBEventHandler] USB drive disconnected");

    // Check if playing from USB - if so, stop playback
    if (context_->fileSource && context_->fileSource->getSource() == FileSource::USB_DRIVE) {
        if (context_->playbackState && context_->playbackState->isPlaying()) {
            // // Serial.println("[USBEventHandler] Stopping playback (USB source removed)");

            // Request stop through coordinator (external interrupt - USB removed)
            // Coordinator handles:
            // - Stopping playback asynchronously
            // - Waiting for all audio cleanup to complete
            // - Navigation (via PlaybackNavigationHandler)
            if (context_->coordinator) {
                context_->coordinator->requestStop(StopReason::EXTERNAL_INTERRUPT);
            }

            // Navigation will be handled by PlaybackNavigationHandler after stop completes
            // Just show notification here
            if (context_->ui) {
                context_->ui->showStatusNotification("USB removed - stopping playback", 3000, DOS_BLACK, DOS_RED);
            }
            return;  // Let coordinator handle the rest
        }
    }

    // If viewing USB file browser, go back to main menu
    if (screenManager_->getCurrentScreenID() == SCREEN_FILE_BROWSER_USB) {
        // // Serial.println("[USBEventHandler] Leaving USB browser (drive removed)");
        screenManager_->switchTo(SCREEN_MAIN_MENU);

        if (context_->ui) {
            context_->ui->showStatusNotification("USB Drive disconnected", 3000, DOS_BLACK, DOS_YELLOW);
        }
        return;  // Exit early, already switched screens
    }

    // Otherwise just show notification (screens handle their own updates via events)
    if (context_->ui) {
        context_->ui->showStatusNotification("USB Drive disconnected", 3000, DOS_BLACK, DOS_YELLOW);
    }
}

// ============================================
// PLAYBACK EVENT HANDLER
// ============================================

ScreenContext* PlaybackEventHandler::context_ = nullptr;
ScreenManager* PlaybackEventHandler::screenManager_ = nullptr;

void PlaybackEventHandler::initialize(ScreenContext* context, ScreenManager* screenManager) {
    context_ = context;
    screenManager_ = screenManager;

    if (!context || !context->eventManager) {
        // // Serial.println("[PlaybackEventHandler] ERROR: Invalid context or event manager");
        return;
    }

    // Subscribe to file error events only
    // Note: Playback navigation is now handled by PlaybackNavigationHandler
    context->eventManager->onStr(EventManager::EVENT_FILE_ERROR, onFileError, nullptr);

    // // Serial.println("[PlaybackEventHandler] Initialized and subscribed to file error events");
}

void PlaybackEventHandler::cleanup() {
    if (context_ && context_->eventManager) {
        context_->eventManager->offAll(nullptr);
    }
    context_ = nullptr;
    screenManager_ = nullptr;
}

void PlaybackEventHandler::onFileError(const char* message, void* userData) {
    (void)userData;

    if (!context_ || !context_->ui) return;

    // // Serial.print("[PlaybackEventHandler] File error: ");
    // // Serial.println(message ? message : "Unknown error");

    // Show error notification
    context_->ui->showStatusNotification(message ? message : "File error", 3000, DOS_BLACK, DOS_RED);
}

// ============================================
// AUDIO EVENT HANDLER
// ============================================

ScreenContext* AudioEventHandler::context_ = nullptr;

void AudioEventHandler::initialize(ScreenContext* context) {
    context_ = context;

    if (!context || !context->eventManager) {
        // // Serial.println("[AudioEventHandler] ERROR: Invalid context or event manager");
        return;
    }

    // Subscribe to audio settings changed event
    context->eventManager->on(EventManager::EVENT_AUDIO_SETTINGS_CHANGED, onAudioSettingsChanged, nullptr);

    // // Serial.println("[AudioEventHandler] Initialized and subscribed to audio settings events");
}

void AudioEventHandler::cleanup() {
    if (context_ && context_->eventManager) {
        context_->eventManager->offAll(nullptr);
    }
    context_ = nullptr;
}

void AudioEventHandler::onAudioSettingsChanged(void* userData) {
    (void)userData;

    if (!context_) return;

    // // Serial.println("[AudioEventHandler] Audio settings changed - applying to audio system");

    // Apply drum sampler setting
    AudioSystem::setDrumSamplerEnabled(
        g_drumSamplerEnabled,
        g_drumSampler,
        context_->opl3,
        mixerLeft,
        mixerRight
    );

    // Apply crossfeed setting (if currently playing MIDI)
    // Note: MidiPlayer will enable/disable these effects during play/stop
    // This just logs the preference for now
    // // Serial.print("[AudioEventHandler] Crossfeed preference: ");
    // // Serial.println(g_crossfeedEnabled ? "ENABLED" : "DISABLED");

    // // Serial.print("[AudioEventHandler] Reverb preference: ");
    // // Serial.println(g_reverbEnabled ? "ENABLED" : "DISABLED");
}
