#include "vgm_player.h"
#include "debug_config.h"  // For DEBUG_SERIAL_ENABLED
#include "audio_system.h"  // For master volume control during fade
#include "audio_globals.h"  // For audioShield access and persistent audio connections
#include <SD.h>
#include <OPL3Duo.h>

// External global settings from main.cpp
extern uint8_t g_maxLoopsBeforeFade;
extern float g_fadeDurationSeconds;
extern bool g_genesisDACEmulation;

// Static member initialization
VGMPlayer* VGMPlayer::instance_ = nullptr;

// Sample rate for VGM files is always 44100 Hz
static const uint32_t VGM_SAMPLE_RATE = 44100;

// Use microsecond-accurate timing
// One sample at 44100 Hz = 22.675737 microseconds
static const float MICROS_PER_SAMPLE = 1000000.0f / 44100.0f;

// Run timer at 5kHz for responsive checking
static const uint32_t TIMER_PERIOD_US = 200; // 5kHz timer

VGMPlayer::VGMPlayer(const PlayerConfig& config)
  : synth_(config.opl3)
  , apu_(config.nesAPU)  // Injected, not owned
  , gbApu_(config.gbAPU)  // Injected, not owned
  , genesisBoard_(config.genesisBoard)  // Injected, not owned
  , dacPrerenderer_(config.dacPrerenderer)  // Injected, not owned
  , dacPrerenderStream_(config.dacPrerenderStream)  // Injected, not owned
  , fileSource_(config.fileSource)
  , state_(PlayerState::IDLE)
  , completionCallback_(nullptr)
  , mixerLeft_(config.mixerChannel1Left)   // Submixer for GB APU/SPC/MOD
  , mixerRight_(config.mixerChannel1Right)
  , dacNesMixerLeft_(config.dacNesMixerLeft)   // DAC/NES pre-mixer for DAC/NES control
  , dacNesMixerRight_(config.dacNesMixerRight) // DAC/NES pre-mixer for DAC/NES control
  , mainMixerLeft_(config.mixerLeft)       // Main mixer for line-in control
  , mainMixerRight_(config.mixerRight)
  , fadeMixerLeft_(config.fadeMixerLeft)
  , fadeMixerRight_(config.fadeMixerRight)
  , sampleCount_(0)
  , pendingDelay_(0)
  , loopEnabled_(true)
  , loopCount_(0)
  , fadeActive_(false)
  , fadeStartTime_(0)
  , loopDurationSamples_(0)
  , loopStartSample_(0)
  , isFinalLoop_(false)
  , timerFlag_(false)
  , nextSampleTime_(0)
  , nextSampleTimeF_(0.0)
  , totalCommands_(0)
  , commandsProcessed_(0)
  , maxProcessTime_(0)
  , hasGenesis_(false)
  , useDACPrerender_(false)  // Will be set in loadFile() based on availability
  , dacPrerendered_(false)   // True if DAC was successfully pre-rendered
  , dacCurrentlyEnabled_(false)  // Tracks whether DAC is currently on
  , debugPsgWrites_(0)
  , debugYmPort0Writes_(0)
  , debugYmPort1Writes_(0) {
  instance_ = this;
  memset(currentFileName_, 0, sizeof(currentFileName_));

  // // Serial.println("[VGMPlayer] Created with PlayerConfig");
}

VGMPlayer::~VGMPlayer() {
  stop();

  // APU is not owned by this player - don't delete it!
  // It's a shared resource managed externally

  instance_ = nullptr;
}

bool VGMPlayer::loadFile(const char* filename) {
  // ALWAYS stop timer and clean up, regardless of state
  // (Previous file might have finished playing but left timer running)
  stopTimer();
  delayMicroseconds(100);
  timerFlag_ = false;

  // CRITICAL: Clean up any previous DAC prerender stream BEFORE loading new file
  // This ensures we don't hear Genesis PCM when switching to NES/OPL/etc
  if (dacPrerendered_ && dacPrerenderStream_) {
    Serial.println("[VGMPlayer] Cleaning up previous DAC prerender before loading new file...");
    dacPrerenderStream_->stop();
    dacPrerenderStream_->closeFile();
    dacNesMixerLeft_->gain(0, 0.0f);   // Mute DAC on pre-mixer channel 0
    dacNesMixerRight_->gain(0, 0.0f);
    if (SD.exists("/TEMP/~dac.tmp")) {
      SD.remove("/TEMP/~dac.tmp");
    }
    dacPrerendered_ = false;
    useDACPrerender_ = false;
  }

  // Perform full hardware reset before loading new file
  synth_->hardwareReset();
  delay(10);

  // Clear any previous file data AFTER hardware reset
  vgmFile_.clear();

  // Reset all playback variables
  sampleCount_ = 0;
  pendingDelay_ = 0;
  timerFlag_ = false;
  commandsProcessed_ = 0;
  maxProcessTime_ = 0;
  nextSampleTime_ = 0;
  nextSampleTimeF_ = 0.0;

  // Reset fade state
  loopCount_ = 0;
  fadeActive_ = false;
  fadeStartTime_ = 0;
  loopDurationSamples_ = 0;
  loopStartSample_ = 0;
  isFinalLoop_ = false;

  // Reset debug counters
  debugPsgWrites_ = 0;
  debugYmPort0Writes_ = 0;
  debugYmPort1Writes_ = 0;

  // Reset DAC state tracking
  dacCurrentlyEnabled_ = false;
  useDACPrerender_ = false;
  dacPrerendered_ = false;

  state_ = PlayerState::LOADING;

  // Store filename
  strncpy(currentFileName_, filename, sizeof(currentFileName_) - 1);
  currentFileName_[sizeof(currentFileName_) - 1] = '\0';

  // // Serial.print("Loading VGM file: ");
  // // Serial.println(filename);

  // Load and parse VGM file (streaming mode)
  bool ok = vgmFile_.loadFromFile(filename, fileSource_);

  if (!ok) {
    state_ = PlayerState::ERROR;
    return false;
  }

  // Check if this VGM is compatible
  ChipType chipType = vgmFile_.getChipType();
  if (chipType == ChipType::NONE) {
    // // Serial.println("VGM file does not contain supported chip data");
    state_ = PlayerState::ERROR;
    return false;
  }

  // Check for Genesis chips
  hasGenesis_ = (chipType == ChipType::SEGA_GENESIS ||
                 chipType == ChipType::YM2612_ONLY ||
                 chipType == ChipType::SN76489_ONLY);

  if (hasGenesis_) {
    if (!genesisBoard_) {
      Serial.println("ERROR: Genesis VGM file but no Genesis board configured!");
      state_ = PlayerState::ERROR;
      return false;
    }

    Serial.println("[VGM] Genesis board detected and configured");

    // Genesis board now uses smart timing - no mode switching needed
    // It tracks time between writes and only delays when necessary
    Serial.println("[VGM] Genesis board initialized (smart timing)");

    genesisBoard_->reset();  // Reset to initial state

    // Configure PSG volume based on chip combination
    if (chipType == ChipType::SEGA_GENESIS) {
      // PSG + YM2612 together: attenuate PSG to blend better
      genesisBoard_->setPSGAttenuation(true);
      Serial.println("[VGM] PSG attenuation ENABLED (playing with YM2612)");
    } else {
      // PSG alone or YM2612 alone: use raw volume
      genesisBoard_->setPSGAttenuation(false);
      Serial.println("[VGM] PSG attenuation DISABLED (raw volume)");
    }

    // If the VGM has DAC samples, configure DAC playback
    if (chipType == ChipType::SEGA_GENESIS || chipType == ChipType::YM2612_ONLY) {
      Serial.println("[VGM] YM2612 DAC channel available for PCM playback");

      // Try DAC pre-rendering (preferred for dense PCM timing accuracy)
      if (g_genesisDACEmulation && dacPrerenderer_ && dacPrerenderStream_) {
        Serial.println("[VGM] Attempting DAC pre-render...");

        // Pre-render the DAC stream to a temp file
        // This expands all DAC commands to a linear 44.1 kHz sample stream
        uint32_t prerenderStart = millis();

        if (dacPrerenderer_->preRender(&vgmFile_, "/TEMP/~dac.tmp")) {
          uint32_t prerenderTime = millis() - prerenderStart;
          Serial.printf("[VGM] DAC pre-render SUCCESS in %lu ms\n", prerenderTime);

          // CRITICAL: Pre-rendering consumed the entire VGM stream.
          // For VGZ (compressed) files, we cannot seek back to position 0.
          // We must reload the VGM file to reset the decompressor state.
          // This also reloads the data bank which was consumed during pre-render.
          Serial.println("[VGM] Reloading VGM file after pre-render...");
          if (!vgmFile_.loadFromFile(filename, fileSource_)) {
            Serial.println("[VGM] WARNING: Failed to reload VGM file after pre-render!");
            dacPrerendered_ = false;
          } else {
            Serial.println("[VGM] VGM file reloaded successfully");

            // Load the pre-rendered file into the playback stream
            if (dacPrerenderStream_->loadFile("/TEMP/~dac.tmp")) {
              useDACPrerender_ = true;
              dacPrerendered_ = true;
              Serial.println("[VGM] Using PRE-RENDERED DAC (perfect timing)");
            } else {
              Serial.println("[VGM] WARNING: Failed to load pre-rendered DAC file");
              dacPrerendered_ = false;
            }
          }
        } else {
          Serial.printf("[VGM] WARNING: DAC pre-render failed: %s\n",
                        dacPrerenderer_->getError() ? dacPrerenderer_->getError() : "unknown error");
          dacPrerendered_ = false;
        }
      }

      // Fall back to hardware DAC if pre-render failed or unavailable
      if (!dacPrerendered_) {
        useDACPrerender_ = false;
        Serial.println("[VGM] Using HARDWARE DAC (may have timing issues with dense PCM)");
      }
    }
  }

  // Set up NES APU if this is a NES APU file
  if (chipType == ChipType::NES_APU) {
    if (!apu_) {
      Serial.println("ERROR: NES APU is required but was not provided in PlayerConfig");
      state_ = PlayerState::ERROR;
      return false;
    }

    Serial.println("[VGM] Using shared NES APU emulator");

    // Reset the APU to clean state (it may have been used by a previous file)
    apu_->reset();

    // AudioConnections are already created globally and stay connected
    // Unmute NES APU on dacNesMixer channel 1 (it was muted after last use)
    // DAC Prerender is on channel 0, NES APU is on channel 1
    dacNesMixerLeft_->gain(1, 0.80f);   // NES APU at 80% on pre-mixer channel 1
    dacNesMixerRight_->gain(1, 0.80f);

    // CRITICAL: Mute line-in (main mixer channel 0) when using NES APU emulator
    // Otherwise we'll hear noise/stuck notes from OPL3/Genesis hardware
    AudioSystem::muteLineIn(*mainMixerLeft_, *mainMixerRight_);
    Serial.println("[VGM] Line-in muted (using NES APU emulator, not hardware)");

    Serial.println("[VGM] NES APU configured for playback (unmuted on pre-mixer ch1)");
  }

  // Set up Game Boy APU if this is a Game Boy file
  if (chipType == ChipType::GAMEBOY_DMG) {
    if (!gbApu_) {
      Serial.println("ERROR: Game Boy APU is required but was not provided in PlayerConfig");
      state_ = PlayerState::ERROR;
      return false;
    }

    Serial.println("[VGM] Using shared Game Boy APU emulator");

    // Reset the APU to clean state (it may have been used by a previous file)
    gbApu_->reset();

    // AudioConnections are already created globally and stay connected
    // Just unmute the submixer channel 2 (it was muted after last use)
    mixerLeft_->gain(2, 0.80f);   // GB APU at 80% on submixer channel 2
    mixerRight_->gain(2, 0.80f);

    // CRITICAL: Mute line-in (main mixer channel 0) when using GB APU emulator
    // Otherwise we'll hear noise/stuck notes from OPL3/Genesis hardware
    AudioSystem::muteLineIn(*mainMixerLeft_, *mainMixerRight_);
    Serial.println("[VGM] Line-in muted (using GB APU emulator, not hardware)");

    Serial.println("[VGM] Game Boy APU configured for playback (unmuted)");
  }

  // Reset playback state
  sampleCount_ = 0;
  pendingDelay_ = 0;
  commandsProcessed_ = 0;
  maxProcessTime_ = 0;

  // Check for loop
  if (vgmFile_.hasLoop()) {
    // // Serial.print("Loop enabled at data offset ");
    // // Serial.println(vgmFile_.getLoopOffsetInData());

    // Calculate loop duration for fade timing
    loopDurationSamples_ = vgmFile_.getLoopSamples();
    // // Serial.print("Loop duration: ");
    // // Serial.print(loopDurationSamples_ / 44100.0f);
    // // Serial.println(" seconds");
  } else {
    loopDurationSamples_ = 0;
  }

  // NOTE: Do NOT call hardwareReset() again here!
  // We already reset at the beginning of loadFile() (line 48)
  // Calling it again causes:
  // - OPL3 mode toggle (off->on) which creates audible transient ("fart noise")
  // - Unnecessary register clearing (already clean)
  // - Extra 10ms+ delay
  // Just configure the chip mode directly:

  // Set OPL3 mode if needed (only for OPL chips, not NES APU, Game Boy, or Genesis)
  if (chipType != ChipType::NES_APU && chipType != ChipType::GAMEBOY_DMG && !hasGenesis_) {
    OPL3Duo* opl = (OPL3Duo*)synth_->getOPL();
    if (chipType == ChipType::YMF262_OPL3 || chipType == ChipType::DUAL_OPL3) {
      // Enable OPL3 mode on both chips
      opl->setOPL3Enabled(0, true);  // Enable OPL3 on chip 0
      if (chipType == ChipType::DUAL_OPL3) {
        opl->setOPL3Enabled(1, true);  // Enable OPL3 on chip 1
        // // Serial.println("Dual OPL3 mode enabled for VGM playback");
      } else {
        // // Serial.println("OPL3 mode enabled for VGM playback");
      }
    } else {
      // OPL2 mode - disable OPL3 features
      opl->setOPL3Enabled(0, false);
      if (chipType == ChipType::DUAL_OPL2) {
        opl->setOPL3Enabled(1, false);
        // // Serial.println("Dual OPL2 mode for VGM playback");
      } else {
        // // Serial.println("OPL2 mode for VGM playback");
      }
    }

    // CRITICAL: Wait for OPL3 mode change to settle
    // YMF262 datasheet specifies settling time after mode changes (reg 0x05)
    // Without this delay, first few register writes may be ignored/misinterpreted
    // This prevents missing audio at the beginning of playback
    delay(5);  // 5ms settling time
  }

  // // Serial.println("\n--- VGM File Ready ---");
  // Serial.print("File: "); // Serial.println(filename);
  // Serial.print("Duration: "); // Serial.print(getDurationMs() / 1000.0f); // Serial.println(" seconds");

  state_ = PlayerState::STOPPED;
  return true;
}

void VGMPlayer::play() {
  if (state_ == PlayerState::PAUSED) {
    // Resume from pause
    resume();
    return;
  }

  if (state_ != PlayerState::STOPPED) {
    // // Serial.println("Cannot play - no file loaded or already playing");
    return;
  }

  // // Serial.println("\nStarting VGM playback...\n");

  // Reset playback position (seek to beginning of data)
  vgmFile_.seekToDataPosition(0);
  sampleCount_ = 0;
  pendingDelay_ = 0;
  commandsProcessed_ = 0;
  nextSampleTimeF_ = (double)micros();  // High-precision start time
  nextSampleTime_ = (uint32_t)nextSampleTimeF_;  // Integer for comparison

  // Reset loop/fade state
  loopCount_ = 0;
  fadeActive_ = false;
  isFinalLoop_ = false;
  loopStartSample_ = 0;

  // Special case: If set to fade after 1 play-through, mark this first play as final
  if (g_maxLoopsBeforeFade == 1) {
    isFinalLoop_ = true;
    loopStartSample_ = 0;  // Fade calculation starts from beginning
    // // Serial.println("Fade after 1 play - will fade on first play-through");
  }

  // Unmute line-in for hardware synths (OPL3 or Genesis)
  // NES/GB APU already muted line-in during loadFile()
  // NOTE: Use chip type, NOT pointer checks! Pointers are shared resources.
  ChipType chipType = vgmFile_.getChipType();

  if (hasGenesis_ && genesisBoard_) {
    // Genesis VGM - unmute line-in with Genesis-specific gain
    AudioSystem::unmuteLineInForGenesis(*mainMixerLeft_, *mainMixerRight_);
    Serial.println("[VGM] Line-in unmuted for Genesis hardware");

    // If using pre-rendered DAC, start the playback stream
    if (useDACPrerender_ && dacPrerendered_ && dacPrerenderStream_) {
      // Unmute DAC on pre-mixer channel 0 for pre-rendered DAC playback
      dacNesMixerLeft_->gain(0, 0.10f);   // DAC at 10% on pre-mixer channel 0
      dacNesMixerRight_->gain(0, 0.10f);

      // Start playback from the pre-rendered file
      dacPrerenderStream_->setLoopEnabled(loopEnabled_);
      dacPrerenderStream_->play();
      Serial.println("[VGM] Pre-rendered DAC playback started (10% volume)");
    }
  } else if (chipType != ChipType::NES_APU && chipType != ChipType::GAMEBOY_DMG) {
    // OPL3 VGM (not NES APU, not GB APU, not Genesis)
    AudioSystem::unmuteLineInForOPL3(*mainMixerLeft_, *mainMixerRight_);
    Serial.println("[VGM] Line-in unmuted for OPL3 hardware");
  }

  // Start the VGM timer
  startTimer();

  // If this is a NES APU file, start the APU's frame timer too
  if (chipType == ChipType::NES_APU && apu_) {
    apu_->startFrameTimer();
    Serial.println("[VGMPlayer] NES APU frame timer started");
  }

  // Start Game Boy APU frame timer if this is a Game Boy file
  if (chipType == ChipType::GAMEBOY_DMG && gbApu_) {
    gbApu_->startFrameTimer();
    Serial.println("[VGMPlayer] Game Boy APU frame timer started");
  }

  // NOTE: Audio routing (unmute) now handled by PlayerManager
  // PlayerManager calls setFadeGain(1.0) before calling play()

  state_ = PlayerState::PLAYING;
}

void VGMPlayer::pause() {
  if (state_ != PlayerState::PLAYING) {
    return;
  }

  stopTimer();
  state_ = PlayerState::PAUSED;
  // // Serial.println("VGM playback paused");
}

void VGMPlayer::resume() {
  if (state_ != PlayerState::PAUSED) {
    return;
  }

  startTimer();
  state_ = PlayerState::PLAYING;
  // // Serial.println("[VGMPlayer] Resumed");
}

void VGMPlayer::stop() {
  if (state_ != PlayerState::PLAYING && state_ != PlayerState::PAUSED) {
    return;
  }

  // // Serial.println("[VGMPlayer] Stopping playback");
  state_ = PlayerState::STOPPING;

  // STEP 1: Stop timer ISR
  stopTimer();

  // STEP 2: Safety delay for ISR to complete
  delay(10);  // 10ms ensures any in-flight ISR fully completes

  // Clear pending flags
  timerFlag_ = false;

  // NOTE: Audio routing (mute) now handled by PlayerManager
  // PlayerManager calls setFadeGain(0.0) after calling stop()
  fadeActive_ = false;

  // STEP 3: Backend-specific cleanup
  // NOTE: Use chip type detection, NOT pointer checks!
  // The APU/GB/Genesis pointers are shared resources that are always non-null.
  // We must check what chip type THIS file was using.
  ChipType chipType = vgmFile_.getChipType();

  if (chipType == ChipType::NES_APU && apu_) {
    // NES APU backend - stop frame timer and mute (but keep connected for reuse)
    Serial.println("[VGMPlayer] APU backend - stopping");

    // Stop the APU's internal frame timer
    apu_->stopFrameTimer();
    Serial.println("[VGMPlayer] APU frame timer stopped");

    // Wait for frame timer ISR to complete
    delay(10);

    // CRITICAL: Reset APU to silence all channels (prevents hung notes)
    apu_->reset();
    Serial.println("[VGMPlayer] NES APU reset (all channels silenced)");

    // Mute NES APU on pre-mixer channel 1 (not submixer)
    // AudioConnections stay connected - they're persistent and shared
    dacNesMixerLeft_->gain(1, 0.0f);   // NES APU on pre-mixer channel 1
    dacNesMixerRight_->gain(1, 0.0f);
    Serial.println("[VGMPlayer] NES APU pre-mixer channel muted (connections stay alive)");

    Serial.println("[VGMPlayer] NES APU stopped (ready for next use)");
  } else if (chipType == ChipType::GAMEBOY_DMG && gbApu_) {
    // Game Boy APU backend - graceful cleanup
    Serial.println("[VGMPlayer] Stopping Game Boy APU...");

    gbApu_->stopFrameTimer();
    Serial.println("[VGMPlayer] Game Boy APU frame timer stopped");

    // Wait for frame timer ISR to complete
    delay(10);

    // CRITICAL: Reset GB APU to silence all channels (prevents hung notes)
    gbApu_->reset();
    Serial.println("[VGMPlayer] Game Boy APU reset (all channels silenced)");

    // Mute submixer channel 2 (GB APU audio)
    // AudioConnections stay connected - they're persistent and shared
    mixerLeft_->gain(2, 0.0f);
    mixerRight_->gain(2, 0.0f);
    Serial.println("[VGMPlayer] GB APU mixer channel muted (connections stay alive)");

    Serial.println("[VGMPlayer] Game Boy APU stopped (ready for next use)");
  } else if (hasGenesis_ && genesisBoard_) {
    // Genesis backend - reset hardware to silence all channels
    Serial.println("[VGMPlayer] Genesis backend - resetting");
    genesisBoard_->reset();  // Silences PSG + keys off all YM2612 channels
    Serial.println("[VGMPlayer] Genesis board reset complete (all notes silenced)");

    // If using pre-rendered DAC, stop and clean up
    if (useDACPrerender_ && dacPrerendered_ && dacPrerenderStream_) {
      Serial.println("[VGMPlayer] Cleaning up pre-rendered DAC...");

      // Stop playback
      dacPrerenderStream_->stop();

      // Close the file (releases file handle)
      dacPrerenderStream_->closeFile();

      // Mute DAC on pre-mixer channel 0 (not submixer)
      dacNesMixerLeft_->gain(0, 0.0f);   // DAC on pre-mixer channel 0
      dacNesMixerRight_->gain(0, 0.0f);

      // Delete the temp file
      if (SD.exists("/TEMP/~dac.tmp")) {
        SD.remove("/TEMP/~dac.tmp");
        Serial.println("[VGMPlayer] Deleted temp DAC file");
      }

      dacPrerendered_ = false;
      Serial.println("[VGMPlayer] Pre-rendered DAC stopped and muted");
    }

    // Note: Line-in muting handled by PlayerManager::centralizedStop()
  } else {
    // OPL3 backend
    // // Serial.println("[VGMPlayer] OPL3 backend - hardware reset");
    synth_->hardwareReset();
    // Note: Line-in muting handled by PlayerManager::centralizedStop()
  }

  // STEP 4: Print debug stats if Genesis
  if (hasGenesis_ && (debugPsgWrites_ > 0 || debugYmPort0Writes_ > 0 || debugYmPort1Writes_ > 0)) {
    Serial.printf("[VGM Genesis] Total writes - PSG: %lu, YM port0: %lu, YM port1: %lu\n",
                  debugPsgWrites_, debugYmPort0Writes_, debugYmPort1Writes_);
  }

  // STEP 5: Reset state
  nextSampleTime_ = 0;
  nextSampleTimeF_ = 0.0;
  sampleCount_ = 0;
  pendingDelay_ = 0;
  loopCount_ = 0;
  isFinalLoop_ = false;
  loopStartSample_ = 0;

  state_ = PlayerState::STOPPED;
  // // Serial.println("[VGMPlayer] Stop complete");
}

void VGMPlayer::reset() {
  stop();

  // Clear the current file
  vgmFile_.clear();
  memset(currentFileName_, 0, sizeof(currentFileName_));

  // Reset all variables
  sampleCount_ = 0;
  pendingDelay_ = 0;
  commandsProcessed_ = 0;

  // Reset synthesizer
  synth_->resetAll();

  state_ = PlayerState::IDLE;
}

void VGMPlayer::update() {
  if (state_ != PlayerState::PLAYING) {
    return;
  }

  // === TIMING DEBUG ===
  static uint32_t debugUpdateCount = 0;
  static uint32_t debugLastReportTime = 0;
  static uint32_t debugMaxUpdateTime = 0;
  static uint32_t debugTotalUpdateTime = 0;
  static uint32_t debugSkippedTimerTicks = 0;

  uint32_t updateStartTime = micros();

  // Update Genesis PCM streams if this is a Genesis file (hardware DAC mode)
  // Note: When using pre-rendered DAC, streams are already baked into the file
  if (hasGenesis_ && genesisBoard_ && !useDACPrerender_) {
    vgmFile_.updateStreams(genesisBoard_);
  }

  // Check if timer has triggered
  if (!timerFlag_) {
    return;
  }
  timerFlag_ = false;

  debugUpdateCount++;

  // === Loop Fade-Out Logic ===
  // Check if we need to start or continue fading out on the final loop
  if (isFinalLoop_ && !fadeActive_ && loopDurationSamples_ > 0) {
    // Calculate fade duration in samples
    uint32_t fadeDurationSamples = (uint32_t)(g_fadeDurationSeconds * 44100.0f);

    // Handle edge case: fade duration longer than loop duration
    if (fadeDurationSamples > loopDurationSamples_) {
      fadeDurationSamples = loopDurationSamples_;  // Fade entire final loop
    }

    // Calculate when fade should start (samples from beginning of this loop)
    uint32_t fadeStartOffset = loopDurationSamples_ - fadeDurationSamples;
    uint32_t currentLoopPosition = sampleCount_ - loopStartSample_;

    // Check if it's time to start the fade
    if (currentLoopPosition >= fadeStartOffset) {
      fadeActive_ = true;
      fadeStartTime_ = millis();
      #if DEBUG_VGM_PLAYBACK
      // // Serial.println("=== Starting fade-out ===");
      #endif
    }
  }

  // Apply fade if active
  if (fadeActive_) {
    uint32_t fadeElapsed = millis() - fadeStartTime_;
    float fadeDurationMS = g_fadeDurationSeconds * 1000.0f;

    // Calculate fade factor using exponential curve (sounds natural, like pro audio)
    float remainingFactor = 1.0f - (fadeElapsed / fadeDurationMS);

    if (remainingFactor <= 0.0f) {
      // Fade complete - stop playback
      #if DEBUG_VGM_PLAYBACK
      // // Serial.println("=== Fade complete - stopping ===");
      #endif
      stop();

      // Notify completion (natural end)
      if (completionCallback_) {
        completionCallback_();
      }
      return;
    }

    // Apply exponential fade curve: fade = remaining^2 (smooth, natural fade)
    float fadeFactor = remainingFactor * remainingFactor;

    // Apply fade to fade mixer (affects both Bluetooth and line-out)
    extern AudioMixer4 fadeMixerLeft;
    extern AudioMixer4 fadeMixerRight;
    AudioSystem::setFadeGain(fadeMixerLeft, fadeMixerRight, fadeFactor);
  }

  // Check if it's time to process the next sample(s)
  uint32_t now = micros();

  // Process all samples that are due
  // Add iteration limiter to prevent infinite loops on corrupted VGM files
  uint16_t iterations = 0;
  constexpr uint16_t MAX_ITERATIONS = 500;  // Increased to allow dense register write bursts
  static uint32_t debugMaxIterationsHit = 0;

  while (now >= nextSampleTime_ && iterations < MAX_ITERATIONS) {
    iterations++;

    if (iterations == MAX_ITERATIONS) {
      debugMaxIterationsHit++;
      Serial.println("[VGM WARNING] Hit MAX_ITERATIONS (500) - may be dropping samples");
    }

    if (pendingDelay_ > 0) {
      // We're waiting for a delay to complete
      pendingDelay_--;
      sampleCount_++;
      // Use double precision to avoid truncation error (22.6757 µs, not 22 µs)
      nextSampleTimeF_ += MICROS_PER_SAMPLE;
      nextSampleTime_ = (uint32_t)(nextSampleTimeF_ + 0.5);  // Round to nearest

      if (pendingDelay_ == 0) {
        // Delay complete, process next commands
        processCommands();

        // Check if playback is done
        if (vgmFile_.isAtEnd()) {
          #if DEBUG_VGM_PLAYBACK
          // // Serial.println("\n=== VGM Playback Complete ===");
          // // Serial.print("Total commands processed: ");
          // // Serial.println(commandsProcessed_);
          #endif
          stop();
          return;
        }
      }
    } else {
      // No delay pending, process commands immediately
      processCommands();

      // If no delay was set, we need to break to avoid infinite loop
      if (pendingDelay_ == 0) {
        // Check if playback is done
        if (vgmFile_.isAtEnd()) {
          #if DEBUG_VGM_PLAYBACK
          // // Serial.println("\n=== VGM Playback Complete ===");
          // // Serial.print("Total commands processed: ");
          // // Serial.println(commandsProcessed_);
          #endif
          stop();
          return;
        }
        break;
      }
    }

    // Prevent getting stuck if we're way behind (safety check)
    // Increased from 1ms to 5ms to allow dense register write bursts to complete
    // Breaking mid-burst causes partial note configuration = harmonic distortion
    if (micros() - now > 5000) {
      debugSkippedTimerTicks++;
      Serial.println("[VGM TIMING WARNING] Spent >5ms processing commands, breaking out");
      break;
    }
  }

  // === SYNCHRONIZE DAC STREAM ===
  // Keep pre-rendered DAC stream aligned with our sample position
  if (useDACPrerender_ && dacPrerendered_ && dacPrerenderStream_) {
    dacPrerenderStream_->setTargetSample(sampleCount_);
  }

  // === TIMING DEBUG - Measure and report ===
  uint32_t updateDuration = micros() - updateStartTime;
  debugTotalUpdateTime += updateDuration;
  if (updateDuration > debugMaxUpdateTime) {
    debugMaxUpdateTime = updateDuration;
  }

  // Report every 2 seconds
  if (millis() - debugLastReportTime > 2000) {
    debugLastReportTime = millis();
    float avgUpdateTime = debugUpdateCount > 0 ? (float)debugTotalUpdateTime / debugUpdateCount : 0;

    Serial.println("=== VGM TIMING REPORT ===");
    Serial.printf("  Updates called: %lu\n", debugUpdateCount);
    Serial.printf("  Avg update time: %.1f μs\n", avgUpdateTime);
    Serial.printf("  Max update time: %lu μs\n", debugMaxUpdateTime);
    Serial.printf("  Commands processed: %lu\n", commandsProcessed_);
    Serial.printf("  Sample position: %lu / %lu (%.1f%%)\n",
                  sampleCount_, vgmFile_.getTotalSamples(),
                  100.0f * sampleCount_ / vgmFile_.getTotalSamples());
    Serial.printf("  Timing drift: %ld μs (nextSample - now)\n", (int32_t)(nextSampleTime_ - micros()));
    Serial.printf("  Skipped >1ms breaks: %lu\n", debugSkippedTimerTicks);
    Serial.printf("  MAX_ITERATIONS hits: %lu\n", debugMaxIterationsHit);
    if (useDACPrerender_ && dacPrerendered_ && dacPrerenderStream_) {
      int32_t drift = dacPrerenderStream_->getSyncDrift();
      Serial.printf("  DAC mode: PRE-RENDERED (sync drift: %ld samples, %.2f ms)\n",
                    drift, drift / 44.1f);
    } else if (hasGenesis_) {
      Serial.println("  DAC mode: HARDWARE (real-time)");
    }
    Serial.println("========================");

    // Reset counters
    debugUpdateCount = 0;
    debugTotalUpdateTime = 0;
    debugMaxUpdateTime = 0;
    debugSkippedTimerTicks = 0;
    debugMaxIterationsHit = 0;
  }
}

void VGMPlayer::processCommands() {
  // Process commands until we hit a delay
  uint32_t commandsThisBatch = 0;
  static uint32_t debugCommandLimitHits = 0;

  while (!vgmFile_.isAtEnd() && pendingDelay_ == 0) {
    processCommand();
    commandsProcessed_++;
    commandsThisBatch++;

    // Limit processing to prevent blocking too long
    // But only return if we've actually processed some commands
    // and we're not at the end of the data
    if (commandsThisBatch >= 1000 && !vgmFile_.isAtEnd()) {
      // We've processed a lot of commands without hitting a delay
      // This might indicate a problem with the VGM file
      debugCommandLimitHits++;
      Serial.printf("[VGM WARNING] Processed 1000 commands without hitting WAIT - breaking (total hits: %lu)\n", debugCommandLimitHits);
      return;
    }
  }
}

/**
 * VGM Playback Timing Model (Genesis.txt alignment)
 *
 * The VGM player uses a two-layer timing model:
 *
 * Layer 1: VGM Sample Timing (WHEN to write)
 *   - VGM wait commands (0x61, 0x62, 0x63, 0x7x) control when commands execute
 *   - Sample-accurate at 44.1kHz (1 sample = 22.7μs)
 *   - Commands scheduled via nextSampleTime_ and pendingDelay_
 *   - This ensures correct music tempo and rhythm
 *
 * Layer 2: Hardware Protocol Timing (HOW to write)
 *   - GenesisBoard enforces chip-specific timing rules:
 *     * PSG: 9μs minimum between writes (32 PSG clock cycles)
 *     * YM2612: 25μs minimum between data writes (32 internal cycles)
 *     * Shift register: 100ns settling after each transfer
 *   - This ensures glitch-free register writes
 *
 * VGM Mode:
 *   - vgmMode_ = true disables extra delays in GenesisBoard
 *   - VGM wait commands provide sufficient spacing
 *   - Hardware primitive delays (WR pulse, settling) still enforced
 *   - Inter-write gaps (9μs, 25μs) still enforced globally
 *
 * Critical: Multiple commands in the same VGM time slice still obey hardware
 * timing rules. If VGM says "write 10 registers at sample 1000", they execute
 * sequentially with proper gaps, potentially spanning multiple samples.
 */
void VGMPlayer::processCommand() {
  if (vgmFile_.isAtEnd()) return;

  uint8_t cmd;
  if (!vgmFile_.readByte(cmd)) {
    return;
  }

  uint8_t reg, val, byte1, byte2, byte3, byte4;

  switch (cmd) {
    case 0x5A: // YM3812 write (chip 0)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL2(reg, val, 0);
      }
      break;

    case 0xAA: // YM3812 write (chip 1)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL2(reg, val, 1);
      }
      break;

    case 0x5E: // YMF262 port 0 write (chip 0)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL3Port0(reg, val, 0);
      }
      break;

    case 0xAE: // YMF262 port 0 write (chip 1)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL3Port0(reg, val, 1);
      }
      break;

    case 0x5F: // YMF262 port 1 write (chip 0)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL3Port1(reg, val, 0);
      }
      break;

    case 0xAF: // YMF262 port 1 write (chip 1)
      if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        writeOPL3Port1(reg, val, 1);
      }
      break;

    case 0xB3: // Game Boy DMG write
      if (gbApu_ && vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        gbApu_->writeRegister(reg, val);
      }
      break;

    case 0xB4: // NES APU write
      if (apu_ && vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
        apu_->writeRegister(reg, val);
      }
      break;

    case 0x61: { // Wait n samples
      uint16_t samples = 0;
      if (vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2)) {
        samples = byte1 | (byte2 << 8);
        waitSamples(samples);
      }
      break;
    }

    case 0x62: // Wait 735 samples (1/60 second)
      waitSamples(735);
      break;

    case 0x63: // Wait 882 samples (1/50 second)
      waitSamples(882);
      break;

    case 0x67: { // Data block
      // Format: 0x67 0x66 tt ss ss ss ss [data]
      // tt = data type, ss = size (32-bit little endian)
      uint8_t check;
      if (!vgmFile_.readByte(check) || check != 0x66) {
        // // Serial.println("VGM: Invalid data block format");
        break;
      }

      uint8_t dataType;
      if (!vgmFile_.readByte(dataType)) break;

      uint32_t dataSize = 0;
      if (!vgmFile_.readByte(byte1)) break;
      if (!vgmFile_.readByte(byte2)) break;
      if (!vgmFile_.readByte(byte3)) break;
      if (!vgmFile_.readByte(byte4)) break;
      dataSize = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

      // Handle different data types
      if (dataType == 0x00) {  // YM2612 PCM data
        // Type 0x00 = YM2612 DAC samples
        if (hasGenesis_ && genesisBoard_) {
          // Allocate temporary buffer to read data block
          uint8_t* blockData = new uint8_t[dataSize];
          if (blockData) {
            // Read all data from VGM file
            bool readSuccess = true;
            for (uint32_t i = 0; i < dataSize; i++) {
              if (!vgmFile_.readByte(blockData[i])) {
                readSuccess = false;
                break;
              }
            }

            if (readSuccess) {
              // Append to Genesis data bank
              vgmFile_.appendToDataBank(blockData, dataSize);
              Serial.print("VGM: Loaded ");
              Serial.print(dataSize);
              Serial.println(" bytes of YM2612 PCM data into data bank");
            } else {
              Serial.println("VGM: Error reading YM2612 PCM data block");
            }

            delete[] blockData;
          } else {
            // Out of memory - skip the block
            Serial.println("VGM: Out of memory reading YM2612 PCM data block");
            for (uint32_t i = 0; i < dataSize; i++) {
              uint8_t dummy;
              if (!vgmFile_.readByte(dummy)) break;
            }
          }
        } else {
          // No Genesis board - skip this data
          for (uint32_t i = 0; i < dataSize; i++) {
            vgmFile_.readByte(byte1);
          }
        }
      } else if (dataType == 0x07 || dataType == 0xC2) {  // NES APU DPCM data
        // Type 0x07 = Compressed data block
        // Type 0xC2 = NES APU RAM write (contains DPCM samples)

        // For type 0xC2, first 2 bytes are the start address
        uint16_t startAddress = 0;
        uint32_t actualDataSize = dataSize;

        if (dataType == 0xC2) {
          // Read start address (2 bytes, little endian)
          uint8_t addrLo, addrHi;
          if (!vgmFile_.readByte(addrLo) || !vgmFile_.readByte(addrHi)) {
            // // Serial.println("VGM: Failed to read RAM write address");
            // Skip remaining data
            for (uint32_t i = 2; i < dataSize; i++) {
              vgmFile_.readByte(byte1);
            }
            break;
          }
          startAddress = addrLo | (addrHi << 8);
          actualDataSize = dataSize - 2;  // Subtract address bytes from data size

          #if DEBUG_VGM_PLAYBACK
          // // Serial.print("VGM: NES APU RAM write at $");
          // // Serial.print(startAddress, HEX);
          // // Serial.print(" (");
          // // Serial.print(actualDataSize);
          // // Serial.println(" bytes)");
          #endif
        } else {
          #if DEBUG_VGM_PLAYBACK
          // // Serial.print("VGM: Loading DPCM data block type 0x07 (");
          // // Serial.print(actualDataSize);
          // // Serial.println(" bytes)");
          #endif
        }

        if (apu_ && actualDataSize > 0 && actualDataSize <= 16384) {  // Sanity check size
          // For type 0xC2, we need to load data at the specific address
          // NES DPCM uses addresses $C000-$FFFF

          if (dataType == 0xC2) {
            // Ensure we have the full 16KB buffer allocated
            apu_->ensureDPCMBuffer();

            // Read data directly into the correct position in the buffer
            // The address tells us where in the $C000-$FFFF range to write
            if (startAddress >= 0xC000 && startAddress < 0x10000) {
              uint16_t offset = startAddress - 0xC000;

              // Create temporary buffer
              uint8_t* tempData = new uint8_t[actualDataSize];
              if (tempData) {
                // Read the data
                bool readSuccess = true;
                for (uint32_t i = 0; i < actualDataSize; i++) {
                  if (!vgmFile_.readByte(tempData[i])) {
                    // // Serial.println("VGM: Failed to read DPCM data");
                    readSuccess = false;
                    break;
                  }
                }

                // Only load if read succeeded
                if (readSuccess) {
                  apu_->loadDPCMDataAtOffset(tempData, actualDataSize, offset);
                }
                delete[] tempData;
              }
            } else {
              #if DEBUG_VGM_PLAYBACK
              // // Serial.print("VGM: Invalid DPCM address $");
              // // Serial.println(startAddress, HEX);
              #endif
              // Skip the data
              for (uint32_t i = 0; i < actualDataSize; i++) {
                vgmFile_.readByte(byte1);
              }
            }
          } else {
            // Type 0x07 - load at start of buffer
            uint8_t* dpcmData = new uint8_t[actualDataSize];
            if (dpcmData) {
              // Read the data from VGM file
              bool readSuccess = true;
              for (uint32_t i = 0; i < actualDataSize; i++) {
                if (!vgmFile_.readByte(dpcmData[i])) {
                  // // Serial.println("VGM: Failed to read DPCM data");
                  readSuccess = false;
                  break;
                }
              }

              // Only load if read succeeded
              if (readSuccess) {
                apu_->loadDPCMData(dpcmData, actualDataSize);
              }

              // Clean up temporary buffer
              delete[] dpcmData;
            } else {
              // // Serial.println("VGM: Failed to allocate DPCM buffer");
              // Skip the data
              for (uint32_t i = 0; i < actualDataSize; i++) {
                vgmFile_.readByte(byte1);
              }
            }
          }
        } else {
          #if DEBUG_VGM_PLAYBACK
          // // Serial.print("VGM: Skipping DPCM data (invalid size or no APU), size=");
          // // Serial.println(actualDataSize);
          #endif
          // Skip the data
          for (uint32_t i = 0; i < actualDataSize; i++) {
            vgmFile_.readByte(byte1);
          }
        }
      } else {
        // Unknown data type - skip it
        #if DEBUG_VGM_PLAYBACK
        // // Serial.print("VGM: Skipping unknown data block type 0x");
        // // Serial.print(dataType, HEX);
        // // Serial.print(" (");
        // // Serial.print(dataSize);
        // // Serial.println(" bytes)");
        #endif
        for (uint32_t i = 0; i < dataSize; i++) {
          vgmFile_.readByte(byte1);
        }
      }
      break;
    }

    case 0x68: { // PCM RAM write
      // Format: 0x68 0x66 cc oo oo oo dd dd dd ss ss ss
      // cc = chip type, oo = read offset, dd = write offset, ss = size
      // Skip the compatibility byte and all 10 data bytes
      uint8_t compat;
      if (!vgmFile_.readByte(compat) || compat != 0x66) break;
      for (int i = 0; i < 10; i++) {
        vgmFile_.readByte(byte1);
      }
      break;
    }

    case 0x66: // End of sound data
      #if DEBUG_VGM_PLAYBACK
      // // Serial.println("End of VGM data");
      #endif
      if (loopEnabled_ && vgmFile_.hasLoop()) {
        // Increment loop count (this counts completed play-throughs)
        loopCount_++;
        #if DEBUG_VGM_PLAYBACK
        // // Serial.print("Completed play-through #");
        // // Serial.println(loopCount_);
        #endif

        // CRITICAL: Reset sample count to loop point position (NOT 0!)
        // VGM files often loop to a point in the middle, not the beginning
        // Example: Song is 60s total (totalSamples), loop section is 40s (loopSamples)
        //          Loop point is at 20s mark (60 - 40 = 20)
        sampleCount_ = vgmFile_.getLoopPointSample();
        #if DEBUG_VGM_PLAYBACK
        // // Serial.print("Jumped to loop point: ");
        // // Serial.print(sampleCount_ / 44100.0f);
        // // Serial.println("s");
        #endif

        // Check if the NEXT play-through should be the final one (with fade)
        // Example: If setting = 2, we want to fade on play-through #2
        // So after completing play-through #1 (loopCount_ = 1), mark next as final
        if (g_maxLoopsBeforeFade > 0 && loopCount_ == (uint32_t)(g_maxLoopsBeforeFade - 1)) {
          isFinalLoop_ = true;
          loopStartSample_ = sampleCount_;  // Track where final play-through begins
          #if DEBUG_VGM_PLAYBACK
          // // Serial.print("Next play-through will be final (fade on play #");
          // // Serial.print(g_maxLoopsBeforeFade);
          // // Serial.println(")");
          #endif
        }

        // Safety check: if we've exceeded the limit, stop (shouldn't happen if fade works)
        if (g_maxLoopsBeforeFade > 0 && loopCount_ >= (uint32_t)g_maxLoopsBeforeFade) {
          #if DEBUG_VGM_PLAYBACK
          // // Serial.println("Exceeded max play-throughs - stopping");
          #endif
          // Mark end of data explicitly (seek doesn't work for VGZ with unknown size)
          vgmFile_.markEndOfData();
        } else {
          // CRITICAL: Reset PCM data bank position when looping
          // Well-formed VGMs should have a 0xE0 command to do this, but reset it here
          // as a safety measure in case the VGM is missing the command
          Serial.print("[VGM Loop] Resetting data bank position from ");
          Serial.print(vgmFile_.getDataBankPosition());
          Serial.println(" to 0");
          vgmFile_.seekDataBank(0);

          // Also reset any active stream positions
          vgmFile_.resetStreamPositions();

          // CRITICAL: If using pre-rendered DAC, tell it to loop back too!
          // The DAC prerender stream runs independently, so we must sync it
          if (useDACPrerender_ && dacPrerendered_ && dacPrerenderStream_) {
            dacPrerenderStream_->seekToLoop();
            Serial.println("[VGM Loop] DAC prerender stream seeked to loop point");
          }

          // Loop back to loop point in file (file seeking - already correct)
          vgmFile_.seekToDataPosition(vgmFile_.getLoopOffsetInData());
          #if DEBUG_VGM_PLAYBACK
          // // Serial.println("Looping...");
          #endif
        }
      } else {
        // No loop or looping disabled - mark end of data to stop playback
        // (seek doesn't work for VGZ/FM9 files with unknown decompressed size)
        vgmFile_.markEndOfData();
      }
      break;

    default:
      if ((cmd & 0xF0) == 0x70) {
        // Wait n+1 samples (0x70-0x7F)
        waitSamples((cmd & 0x0F) + 1);
      } else if ((cmd & 0xF0) == 0x80) {
        // YM2612 port 0 address 2A (DAC) write from data bank, then wait n samples (0x80-0x8F)
        // NOTE: These commands read from the PCM data bank, NOT from the command stream!
        // The PCM data should have been loaded via command 0x67 data blocks
        uint8_t waitSampleCount = cmd & 0x0F;

        if (genesisBoard_ && hasGenesis_) {
          // Read next byte from PCM data bank
          uint8_t sample;
          vgmFile_.readDataBankByte(sample);  // Returns silence (0x80) if bank is empty

          // Write PCM sample to DAC - route based on mode
          if (useDACPrerender_ && dacPrerendered_) {
            // Pre-rendered DAC - samples played from file, nothing to do here
            // Data bank position still advanced above for stream commands
          } else {
            // Hardware DAC - writeDAC handles streaming mode internally
            genesisBoard_->writeDAC(sample);
          }
        }

        // Wait using VGM's sample-accurate timing system (not blocking delays!)
        if (waitSampleCount > 0) {
          waitSamples(waitSampleCount);
        }
      } else if (cmd >= 0x30 && cmd <= 0x3F) {
        // 0x30 = second PSG write (dual-chip), 0x31-0x3F = reserved
        // All take 1 byte parameter
        vgmFile_.readByte(byte1);
      } else if (cmd >= 0x40 && cmd <= 0x4E) {
        // Various YM2612 commands - skip for OPL
        uint8_t skipBytes = 2;
        if (cmd == 0x4F) skipBytes = 1; // Game Gear PSG
        for (uint8_t i = 0; i < skipBytes; i++) {
          vgmFile_.readByte(byte1);
        }
      } else if (cmd == 0x50) {
        // PSG (SN76489) write
        if (vgmFile_.readByte(val)) {
          if (genesisBoard_ && hasGenesis_) {
            genesisBoard_->writePSG(val);
            debugPsgWrites_++;
          }
        }
      } else if (cmd == 0x52) {
        // YM2612 port 0 write
        if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
          if (genesisBoard_ && hasGenesis_) {
            // Special handling for DAC register
            if (reg == 0x2A) {
              // DAC data write - route based on mode
              if (useDACPrerender_ && dacPrerendered_) {
                // Pre-rendered DAC - samples played from file, nothing to do here
              } else {
                // Hardware DAC - writeDAC handles streaming mode internally
                genesisBoard_->writeDAC(val);
              }
            } else if (reg == 0x2B) {
              // Register 0x2B: bit 7 = DAC enable, bits 0-4 = timer control
              bool dacEnabled = (val & 0x80) != 0;

              // Track DAC state (needed for 0xB6 handling)
              dacCurrentlyEnabled_ = dacEnabled;

              if (useDACPrerender_ && dacPrerendered_) {
                // Pre-rendered DAC - DAC enable is baked into the pre-rendered file
                // Just write timer bits to hardware
                uint8_t hardwareVal = val & 0x7F;  // Clear DAC enable bit
                genesisBoard_->writeYM2612(0, reg, hardwareVal);
              } else {
                // Hardware DAC mode - write everything including DAC enable
                genesisBoard_->enableDAC(dacEnabled);
                genesisBoard_->writeYM2612(0, reg, val);
              }
            } else {
              // Regular YM2612 register write
              genesisBoard_->writeYM2612(0, reg, val);
              // DEBUG: Disabled to avoid serial spam
            }
            debugYmPort0Writes_++;
          }
        }
      } else if (cmd == 0x53) {
        // YM2612 port 1 write
        if (vgmFile_.readByte(reg) && vgmFile_.readByte(val)) {
          if (genesisBoard_ && hasGenesis_) {
            // Special handling for channel 6 panning (register 0xB6)
            if (reg == 0xB6) {
              // Channel 6 output control
              if (useDACPrerender_ && dacPrerendered_) {
                // Pre-rendered DAC - panning is baked into the pre-rendered file
                // If DAC is disabled, channel 6 is FM - write to hardware
                if (!dacCurrentlyEnabled_) {
                  genesisBoard_->writeYM2612(1, reg, val);
                }
                // If DAC is enabled, panning comes from pre-rendered file, skip write
              } else {
                // Hardware DAC mode - write to hardware
                genesisBoard_->writeYM2612(1, reg, val);
              }
            } else {
              // Write to hardware for all other registers (FM channels, etc.)
              genesisBoard_->writeYM2612(1, reg, val);
            }

            debugYmPort1Writes_++;
            // DEBUG: Disabled to avoid serial spam
          }
        }
      } else if ((cmd >= 0x51 && cmd <= 0x5D) && cmd != 0x52 && cmd != 0x53) {
        // Other sound chip writes we don't handle yet (EXCEPT 0x52/0x53 which are handled above!)
        vgmFile_.readByte(byte1);
        vgmFile_.readByte(byte2);
      } else if (cmd >= 0xA0 && cmd <= 0xBF) {
        // Various second chip writes - some we handle above, skip unknown ones
        vgmFile_.readByte(byte1);
        vgmFile_.readByte(byte2);
      } else if (cmd >= 0xC0 && cmd <= 0xDF) {
        // Various third/fourth chip writes - skip
        vgmFile_.readByte(byte1);
        vgmFile_.readByte(byte2);
        vgmFile_.readByte(byte3);
      } else if (cmd == 0x90) {
        // Setup Stream Control - 5 bytes: stream_id, chip_type, port, command
        uint8_t streamID, chipType, port, command;
        if (vgmFile_.readByte(streamID) && vgmFile_.readByte(chipType) &&
            vgmFile_.readByte(byte1) && vgmFile_.readByte(port) && vgmFile_.readByte(command)) {
          // byte1 is reserved, just skip it
          if (hasGenesis_) {
            vgmFile_.setupStream(streamID, chipType, port, command);
          }
        }
      } else if (cmd == 0x91) {
        // Set Stream Data - 5 bytes: stream_id, data_bank_id, step_size, step_base
        uint8_t streamID, dataBankID, stepSize, stepBase;
        if (vgmFile_.readByte(streamID) && vgmFile_.readByte(dataBankID) &&
            vgmFile_.readByte(stepSize) && vgmFile_.readByte(stepBase)) {
          if (hasGenesis_) {
            vgmFile_.setStreamData(streamID, dataBankID, stepSize, stepBase);
          }
        }
      } else if (cmd == 0x92) {
        // Set Stream Frequency - 5 bytes: stream_id, frequency (32-bit)
        uint8_t streamID;
        if (vgmFile_.readByte(streamID) &&
            vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2) &&
            vgmFile_.readByte(byte3) && vgmFile_.readByte(byte4)) {
          uint32_t frequency = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);
          if (hasGenesis_) {
            vgmFile_.setStreamFrequency(streamID, frequency);
          }
        }
      } else if (cmd == 0x93) {
        // Start Stream - 11 bytes: stream_id, data_start (32-bit), length_mode, data_length (32-bit)
        uint8_t streamID, lengthMode;
        if (vgmFile_.readByte(streamID) &&
            vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2) &&
            vgmFile_.readByte(byte3) && vgmFile_.readByte(byte4)) {
          uint32_t dataStart = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

          if (vgmFile_.readByte(lengthMode) &&
              vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2) &&
              vgmFile_.readByte(byte3) && vgmFile_.readByte(byte4)) {
            uint32_t dataLength = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

            if (hasGenesis_) {
              vgmFile_.startStream(streamID, dataStart, lengthMode, dataLength);
            }
          }
        }
      } else if (cmd == 0x94) {
        // Stop Stream - 1 byte: stream_id
        uint8_t streamID;
        if (vgmFile_.readByte(streamID)) {
          if (hasGenesis_) {
            vgmFile_.stopStream(streamID);
          }
        }
      } else if (cmd == 0x95) {
        // Start Stream (fast call) - 4 bytes: stream_id, block_id (16-bit), flags
        uint8_t streamID, flags;
        if (vgmFile_.readByte(streamID) &&
            vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2) &&
            vgmFile_.readByte(flags)) {
          uint16_t blockID = byte1 | (byte2 << 8);
          if (hasGenesis_) {
            vgmFile_.startStreamFast(streamID, blockID, flags);
          }
        }
      } else if (cmd >= 0xE0 && cmd <= 0xFF) {
        // Seek or data stream commands
        if (cmd == 0xE0) {
          // Seek to offset in PCM data bank - 4 bytes (32-bit little endian offset)
          if (vgmFile_.readByte(byte1) && vgmFile_.readByte(byte2) &&
              vgmFile_.readByte(byte3) && vgmFile_.readByte(byte4)) {
            uint32_t offset = byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24);

            if (hasGenesis_) {
              vgmFile_.seekDataBank(offset);
            }
          }
        } else {
          // Most others are 2 bytes - skip them
          vgmFile_.readByte(byte1);
          vgmFile_.readByte(byte2);
        }
      } else {
        // Truly unknown command
        #if DEBUG_VGM_PLAYBACK
        // // Serial.print("ERROR: Unknown VGM command at offset ");
        // // Serial.print(vgmFile_.getCurrentDataPosition() - 1);
        // // Serial.print(": 0x");
        // // Serial.print(cmd, HEX);
        // // Serial.println(" - stopping playback");
        #endif
        // Seek to end to force stop
        vgmFile_.seekToDataPosition(vgmFile_.getDataSize());
      }
      break;
  }
}

void VGMPlayer::writeOPL2(uint8_t reg, uint8_t val, uint8_t chip) {
  // OPL2 mode write to specified chip
  // For OPL3 Duo, chip 0 is synthUnit 0, chip 1 is synthUnit 1
  OPL3Duo* opl = (OPL3Duo*)synth_->getOPL();

  // Use setChipRegister to write to the specific chip
  // Register range for OPL2 is 0x00-0xFF (bank 0)
  opl->setChipRegister(chip & 1, reg, val);
}

void VGMPlayer::writeOPL3Port0(uint8_t reg, uint8_t val, uint8_t chip) {
  // OPL3 port 0 (registers 0x00-0xFF, bank 0)
  OPL3Duo* opl = (OPL3Duo*)synth_->getOPL();

  // Use setChipRegister to write to the specific chip
  opl->setChipRegister(chip & 1, reg, val);
}

void VGMPlayer::writeOPL3Port1(uint8_t reg, uint8_t val, uint8_t chip) {
  // OPL3 port 1 (registers 0x100-0x1FF, bank 1)
  OPL3Duo* opl = (OPL3Duo*)synth_->getOPL();

  // For bank 1, add 0x100 to the register
  opl->setChipRegister(chip & 1, reg | 0x100, val);
}

void VGMPlayer::waitSamples(uint32_t samples) {
  pendingDelay_ = samples;
}

uint32_t VGMPlayer::calculateDelayMicros(uint32_t samples) {
  // Convert samples at 44100 Hz to microseconds
  return (samples * 1000000UL) / VGM_SAMPLE_RATE;
}

float VGMPlayer::getProgress() const {
  uint32_t total = vgmFile_.getTotalSamples();
  if (total == 0) return 0.0f;
  return (float)sampleCount_ / (float)total;
}

uint32_t VGMPlayer::getDurationMs() const {
  // Convert total samples to milliseconds
  // VGM sample rate is always 44100 Hz
  return (vgmFile_.getTotalSamples() * 1000UL) / 44100UL;
}

uint32_t VGMPlayer::getPositionMs() const {
  // Convert current sample position to milliseconds
  return (sampleCount_ * 1000UL) / 44100UL;
}

void VGMPlayer::printStats() const {
  // // Serial.println("\n--- VGM Player Statistics ---");
  // // Serial.print("State: ");
  switch (state_) {
    // case PlayerState::IDLE: // Serial.println("IDLE"); break;
    // case PlayerState::LOADING: // Serial.println("LOADING"); break;
    // case PlayerState::READY: // Serial.println("READY"); break;
    // case PlayerState::PLAYING: // Serial.println("PLAYING"); break;
    // case PlayerState::PAUSED: // Serial.println("PAUSED"); break;
    // case PlayerState::STOPPING: // Serial.println("STOPPING"); break;
    // case PlayerState::STOPPED: // Serial.println("STOPPED"); break;
    // case PlayerState::ERROR: // Serial.println("ERROR"); break;
  }
  // // Serial.print("File: ");
  // // Serial.println(currentFileName_[0] ? currentFileName_ : "(none)");
  // // Serial.print("Progress: ");
  // // Serial.print(getProgress() * 100.0f);
  // // Serial.println("%");
  // // Serial.print("Sample: ");
  // // Serial.print(sampleCount_);
  // // Serial.print(" / ");
  // // Serial.println(vgmFile_.getTotalSamples());
  // // Serial.print("Commands processed: ");
  // // Serial.println(commandsProcessed_);
  // // Serial.print("Max process time: ");
  // // Serial.print(maxProcessTime_);
  // // Serial.println(" us");

  // Genesis debug counters (if Genesis VGM)
  if (hasGenesis_) {
    Serial.printf("[VGM Genesis] FINAL TOTALS - PSG writes=%lu, YM port0=%lu, YM port1=%lu\n",
                  debugPsgWrites_, debugYmPort0Writes_, debugYmPort1Writes_);
    Serial.printf("[VGM Genesis] If FM/PSG counts are high but you heard nothing, check hardware wiring/power\n");
  }

  // // Serial.println("-----------------------------");
}

// Timer management
void VGMPlayer::onTimerISR() {
  if (instance_) {
    instance_->timerFlag_ = true;
  }
}

void VGMPlayer::startTimer() {
  // Always stop any existing timer first
  timer_.end();
  delayMicroseconds(100);

  // Clear any pending flags
  timerFlag_ = false;

  // Start new timer
  timer_.begin(onTimerISR, TIMER_PERIOD_US);
}

void VGMPlayer::stopTimer() {
  // Stop the timer
  timer_.end();

  // Add small delay to ensure timer is fully stopped
  delayMicroseconds(100);

  // Clear flag after timer is stopped
  timerFlag_ = false;

  // Clear the static instance pointer temporarily to prevent ISR access
  VGMPlayer* temp = instance_;
  instance_ = nullptr;
  delayMicroseconds(100);
  instance_ = temp;
}

