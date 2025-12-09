#include "midi_player.h"
#include "debug_config.h"  // For DEBUG_SERIAL_ENABLED
#include "audio_system.h"  // For audio control
#include <SD.h>
#include <OPL3Duo.h>

// Static member initialization
MidiPlayer* MidiPlayer::instance_ = nullptr;

MidiPlayer::MidiPlayer(const PlayerConfig& config)
  : synth_(config.opl3)
  , fileSource_(config.fileSource)
  , drumSampler_(config.drumSampler)
  , mixerLeft_(config.mixerLeft)
  , mixerRight_(config.mixerRight)
  , fadeMixerLeft_(config.fadeMixerLeft)
  , fadeMixerRight_(config.fadeMixerRight)
  , finalMixerLeft_(config.finalMixerLeft)
  , finalMixerRight_(config.finalMixerRight)
  , reverbLeft_(config.reverbLeft)
  , reverbRight_(config.reverbRight)
  , crossfeedEnabled_(config.crossfeedEnabled)
  , reverbEnabled_(config.reverbEnabled)
  , state_(PlayerState::IDLE)
  , completionCallback_(nullptr)
  , tickCount_(0)
  , lastDispatchedTick_(0)
  , eventCount_(0)
  , estimatedTotalTicks_(0)
  , lastStatsTime_(0)
  , showFirstEvents_(true) {
  instance_ = this;  // Set static instance for ISR
  memset(currentFileName_, 0, sizeof(currentFileName_));

  // // Serial.println("[MidiPlayer] Created with PlayerConfig");
}

MidiPlayer::~MidiPlayer() {
  stop();
  instance_ = nullptr;
}

bool MidiPlayer::loadFile(const char* filename) {
  // ALWAYS stop timer and clean up, regardless of state
  // (Previous file might have finished playing but left timer running)
  stopTickTimer();
  delayMicroseconds(100);

  // Perform full hardware reset before loading new file
  synth_->hardwareReset();
  delay(10);

  // Reset all playback variables
  tickCount_ = 0;
  lastDispatchedTick_ = 0;
  eventCount_ = 0;
  lastStatsTime_ = 0;
  showFirstEvents_ = true;
  estimatedTotalTicks_ = 0;  // Will update during playback

  state_ = PlayerState::LOADING;

  // Store filename
  strncpy(currentFileName_, filename, sizeof(currentFileName_) - 1);
  currentFileName_[sizeof(currentFileName_) - 1] = '\0';

  // // Serial.print("Loading MIDI file: ");
  // // Serial.println(filename);

  // Clear any existing MIDI data
  midi_.clear();

  // Load using streaming parser (or legacy parser via compatibility wrapper)
  bool ok = midi_.loadFromFile(filename, fileSource_);

  if (!ok) {
    // // Serial.println("Invalid or unsupported MIDI file.");
    state_ = PlayerState::ERROR;
    return false;
  }

  // Reset playback variables
  tickCount_ = 0;
  lastDispatchedTick_ = 0;
  eventCount_ = 0;
  lastStatsTime_ = 0;
  showFirstEvents_ = true;
  estimatedTotalTicks_ = 0;  // Will update during playback

  // NOTE: Do NOT call hardwareReset() again here!
  // We already reset at the beginning of loadFile() (line 34)
  // Calling it again causes:
  // - OPL3 mode toggle (off->on) which creates audible transient ("fart noise")
  // - Unnecessary register clearing (already clean)
  // - Extra 10ms+ delay
  // Just configure the chip mode directly:

  // Ensure OPL3 mode is enabled on both chips for MIDI playback
  // VGM files might have disabled OPL3 mode or set it to OPL2 mode
  OPL3Duo* opl = (OPL3Duo*)synth_->getOPL();
  opl->setOPL3Enabled(0, true);  // Enable OPL3 on chip 0
  opl->setOPL3Enabled(1, true);  // Enable OPL3 on chip 1

  // CRITICAL: Wait for OPL3 mode change to settle
  // YMF262 datasheet specifies settling time after mode changes (reg 0x05)
  // Without this delay, first few MIDI events may not sound correctly
  delay(5);  // 5ms settling time

  // // Serial.println("\n--- MIDI File Loaded ---");
  // Serial.print("File: "); // Serial.println(filename);
  // Serial.print("PPQN: "); // Serial.println(midi_.ppqn());
  // Serial.print("Initial tempo: "); // Serial.print(60000000 / midi_.initialTempoUSQ()); // Serial.println(" BPM");

  // Scan file to find total duration
  // // Serial.println("Scanning MIDI file for total duration...");
  scanFileDuration();
  // Serial.print("Total ticks: "); // Serial.println(estimatedTotalTicks_);
  // Serial.print("Estimated duration: "); Serial.print(getDuration()); // Serial.println(" seconds");

  state_ = PlayerState::STOPPED;
  return true;
}

void MidiPlayer::play() {
  if (state_ != PlayerState::STOPPED) {
    // // Serial.println("[MidiPlayer] Cannot play - no file loaded or already playing");
    return;
  }

  // // Serial.println("[MidiPlayer] Starting playback");

  // NOTE: Audio routing (unmute, effects) now handled by PlayerManager
  // PlayerManager calls enableCrossfeed/enableReverb before calling play()
  // PlayerManager calls setFadeGain(1.0) before calling play()

  // Start the tick timer
  updateTickTimer(midi_.usPerTick());
  startTickTimer(midi_.usPerTick());

  state_ = PlayerState::PLAYING;
  // // Serial.println("[MidiPlayer] Playback started");
}

void MidiPlayer::stop() {
  if (state_ != PlayerState::PLAYING && state_ != PlayerState::PAUSED) {
    return;
  }

  // // Serial.println("[MidiPlayer] Stopping playback");
  state_ = PlayerState::STOPPING;

  // STEP 1: Stop timer ISR
  stopTickTimer();

  // STEP 2: Safety delay for ISR to complete
  // 10ms ensures any in-flight ISR fully completes
  delay(10);

  // NOTE: Audio routing (mute, effects) now handled by PlayerManager
  // PlayerManager calls setFadeGain(0.0) after calling stop()
  // PlayerManager calls enableCrossfeed/enableReverb(false) after calling stop()

  // STEP 3: Silence hardware
  synth_->hardwareReset();

  // STEP 4: Update state
  state_ = PlayerState::STOPPED;
  // // Serial.println("[MidiPlayer] Stop complete");
}

void MidiPlayer::replay() {
  if (state_ == PlayerState::IDLE || state_ == PlayerState::ERROR) {
    // // Serial.println("No file loaded to replay");
    return;
  }

  // Stop current playback if active
  if (state_ == PlayerState::PLAYING) {
    stopTickTimer();
    synth_->allNotesOff();
  }

  // // Serial.println("\nReplaying file...");

  // Store the current filename before reloading
  char filenameCopy[64];
  strncpy(filenameCopy, currentFileName_, sizeof(filenameCopy));
  filenameCopy[sizeof(filenameCopy) - 1] = '\0';

  // Reload the file (streaming implementation requires this)
  if (!loadFile(filenameCopy)) {
    // // Serial.println("Failed to reload file for replay");
    state_ = PlayerState::ERROR;
    return;
  }

  // Start playback
  play();
}

void MidiPlayer::reset() {
  // Stop playback if active
  stop();

  // Clear the current file name
  memset(currentFileName_, 0, sizeof(currentFileName_));

  // Reset all state variables
  tickCount_ = 0;
  lastDispatchedTick_ = 0;
  eventCount_ = 0;
  lastStatsTime_ = 0;
  showFirstEvents_ = true;

  // Perform full hardware reset
  synth_->hardwareReset();

  // Clear the MIDI song
  midi_.clear();

  state_ = PlayerState::IDLE;
}

void MidiPlayer::update() {
  if (state_ != PlayerState::PLAYING) {
    return;
  }

  // Process MIDI events
  processEvents();

  // Check if playback is done
  if (midi_.playbackDone(lastDispatchedTick_)) {
    // // Serial.println("\n=== Playback Complete ===");
    // // Serial.print("Total events processed: ");
    // // Serial.println(eventCount_);
    stop();

    // Notify completion (natural end)
    if (completionCallback_) {
      completionCallback_();
    }
  }
}

void MidiPlayer::processEvents() {
  // Get current tick with interrupts disabled
  noInterrupts();
  const uint32_t nowTick = tickCount_;
  interrupts();

  // Drain events up to 'nowTick'
  MidiEvent ev;
  while (midi_.peekEvent(ev) && ev.tick <= nowTick) {
    midi_.popEvent(ev);
    eventCount_++;

    // Track furthest tick for duration estimation
    if (ev.tick > estimatedTotalTicks_) {
      estimatedTotalTicks_ = ev.tick;
    }

    #if DEBUG_SERIAL_ENABLED
    // Show first few events for debugging
    if (showFirstEvents_ && eventCount_ <= 5) {
      // // Serial.print("Event ");
      // // Serial.print(eventCount_);
      // // Serial.print(": ");

      if (ev.type == MidiEventType::NoteOn) {
        // // Serial.print("NoteOn ch=");
        // // Serial.print(ev.channel);
        // // Serial.print(" key=");
        // // Serial.print(ev.key);
        // // Serial.print(" vel=");
        // // Serial.println(ev.velocity);
      } else {
        // // Serial.println(static_cast<int>(ev.type));
      }

      if (eventCount_ >= 5) {
        showFirstEvents_ = false;
      }
    }
    #endif

    dispatchEvent(ev);
    lastDispatchedTick_ = ev.tick;
  }
}

void MidiPlayer::dispatchEvent(const MidiEvent& ev) {
  // MIDI channel 10 (index 9) = GM drum channel
  // Route to drum sampler if available and enabled
  bool isDrumChannel = (ev.channel == 9);
  bool useDrumSampler = isDrumChannel && drumSampler_ && drumSampler_->isEnabled();

  switch (ev.type) {
    case MidiEventType::NoteOn:
      if (useDrumSampler) {
        drumSampler_->noteOn(ev.key, ev.velocity);
      } else {
        synth_->noteOn(ev.channel, ev.key, ev.velocity, ev.tick);
      }
      break;
    case MidiEventType::NoteOff:
      if (useDrumSampler) {
        drumSampler_->noteOff(ev.key);
      } else {
        synth_->noteOff(ev.channel, ev.key, ev.velocity);
      }
      break;
    case MidiEventType::ProgramChange:
      // Don't send program changes to drum sampler
      if (!useDrumSampler) {
        synth_->programChange(ev.channel, ev.value1);
      }
      break;
    case MidiEventType::ChannelPressure:
      if (!useDrumSampler) {
        synth_->channelPressure(ev.channel, ev.value1);
      }
      break;
    case MidiEventType::PitchBend:
      if (!useDrumSampler) {
        synth_->pitchBend(ev.channel, ev.pitchBend);
      }
      break;
    case MidiEventType::ControlChange:
      if (!useDrumSampler) {
        synth_->controlChange(ev.channel, ev.value1, ev.value2);
      }
      break;
    case MidiEventType::MetaTempo:
      // Update µs/tick and reconfigure timer
      midi_.applyTempoChange(ev.tempoUSQ);
      updateTickTimer(midi_.usPerTick());
      break;
    case MidiEventType::EndOfTrack:
      // ignore; player stops on last event naturally
      break;
    default:
      break;
  }
}

void MidiPlayer::printStats() const {
  // // Serial.println("\n--- Player Statistics ---");
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
  // // Serial.print("Current file: ");
  // // Serial.println(currentFileName_[0] ? currentFileName_ : "(none)");
  // // Serial.print("Events processed: ");
  // // Serial.println(eventCount_);
  // // Serial.print("Current tick: ");
  // // Serial.println(lastDispatchedTick_);
  synth_->printVoiceStats();
  // // Serial.println("-------------------------");
}

// Timer management
void MidiPlayer::onTickISR() {
  if (instance_) {
    instance_->tickCount_++;
  }
}

void MidiPlayer::startTickTimer(uint32_t us_per_tick) {
  // Always stop any existing timer first
  tickTimer_.end();
  delayMicroseconds(100);

  // Clear tick count
  noInterrupts();
  tickCount_ = 0;
  interrupts();

  // Start new timer
  tickTimer_.begin(onTickISR, us_per_tick);
}

void MidiPlayer::updateTickTimer(uint32_t us_per_tick) {
  tickTimer_.update(us_per_tick);
}

void MidiPlayer::stopTickTimer() {
  // Stop the timer
  tickTimer_.end();

  // Add small delay to ensure timer is fully stopped
  delayMicroseconds(100);

  // Clear the static instance pointer temporarily to prevent ISR access
  MidiPlayer* temp = instance_;
  instance_ = nullptr;
  delayMicroseconds(100);
  instance_ = temp;
}

float MidiPlayer::getDuration() const {
  // Estimate duration based on furthest tick seen and current tempo
  if (estimatedTotalTicks_ == 0) return 0.0f;

  // Duration (seconds) = (ticks × us_per_tick) / 1,000,000
  uint32_t usPerTick = midi_.usPerTick();
  float durationSeconds = (float)(estimatedTotalTicks_ * usPerTick) / 1000000.0f;

  return durationSeconds;
}

float MidiPlayer::getProgress() const {
  // Progress based on current tick vs estimated total
  if (estimatedTotalTicks_ == 0) return 0.0f;

  uint32_t currentTick = lastDispatchedTick_;
  return min(1.0f, (float)currentTick / (float)estimatedTotalTicks_);
}

void MidiPlayer::scanFileDuration() {
  // Scan through all events to find the last tick
  // This gives us accurate total duration upfront
  estimatedTotalTicks_ = 0;
  uint32_t maxTick = 0;
  uint32_t eventCount = 0;

  MidiEvent ev;
  while (midi_.peekEvent(ev)) {
    if (ev.tick > maxTick) {
      maxTick = ev.tick;
    }

    midi_.popEvent(ev);  // Consume the event
    eventCount++;

    // Safety limit to prevent infinite loop
    if (eventCount > 100000) {
      // // Serial.println("Warning: Stopped scanning after 100k events");
      break;
    }
  }

  estimatedTotalTicks_ = maxTick;
  // // Serial.print("Scanned ");
  // // Serial.print(eventCount);
  // // Serial.println(" events");

  // Reset playback - we need to reload the file since we consumed all events
  // For streaming parser, this requires closing and reopening
  const char* savedFilename = currentFileName_;
  midi_.clear();

  // Reload the file
  if (!midi_.loadFromFile(savedFilename, fileSource_)) {
    // // Serial.println("ERROR: Failed to reload MIDI file after scanning!");
    state_ = PlayerState::ERROR;
  }
}

// ============================================
// IAudioPlayer INTERFACE IMPLEMENTATIONS
// ============================================

void MidiPlayer::pause() {
  if (state_ != PlayerState::PLAYING) {
    return;
  }

  stopTickTimer();
  state_ = PlayerState::PAUSED;
  // // Serial.println("[MidiPlayer] Paused");
}

void MidiPlayer::resume() {
  if (state_ != PlayerState::PAUSED) {
    return;
  }

  startTickTimer(midi_.usPerTick());
  state_ = PlayerState::PLAYING;
  // // Serial.println("[MidiPlayer] Resumed");
}

uint32_t MidiPlayer::getDurationMs() const {
  // Convert from seconds to milliseconds
  return (uint32_t)(getDuration() * 1000.0f);
}

uint32_t MidiPlayer::getPositionMs() const {
  // Calculate position from current tick and tempo
  float durationSec = getDuration();
  float progress = getProgress();
  return (uint32_t)(durationSec * progress * 1000.0f);
}