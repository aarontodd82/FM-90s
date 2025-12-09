#include <Arduino.h>
#include <SD.h>
#include <Audio.h>
#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <SPI.h>
#include "hardware_initializer.h"  // Centralized hardware initialization
#include "audio_system.h"          // Centralized audio configuration
#include "player_manager.h"        // Unified player management (replaces PlayerFactory + PlaybackController)
#include "playback_coordinator.h"  // Event-driven playback lifecycle coordinator
#include "opl3_synth.h"
#include "file_browser.h"
#include "floppy_manager.h"
#include "usb_drive_manager.h"
#include "file_source.h"
// menu_system.h removed - legacy serial menu deprecated
#include "display_manager.h"  // Dual display manager (RA8875 TFT + LCD Shield)
#include "ui/screen_manager.h"  // Screen navigation system
#include "ui/framework/screen_context.h"  // Framework dependency injection
#include "ui/framework/system_event_handlers.h"  // System event handlers
#include "ui/lcd_manager.h"  // Smart LCD update manager
#include "ui/framework/playback_navigation_handler.h"  // Playback navigation decisions
#include "playback_state.h"  // Global playback state tracking
#include "drum_sampler_v2.h"  // PCM drum sampler (AudioPlayMemory + PROGMEM samples)
#include "nes_apu_emulator.h"  // NES APU emulator (AudioStream for VGM NES files)
#include "gameboy_apu.h"       // Game Boy APU emulator (AudioStream for VGM GB files)
#include "genesis_board.h"      // Genesis synthesizer board (YM2612 + SN76489)
#include "audio_stream_spc.h"  // AudioStreamSPC (AudioStream for SNES files) - SEPARATE FILE TO AVOID ODR
#include "spc_player.h"  // SPC player (uses AudioStreamSPC)
#include "dac_prerender.h"     // DAC pre-renderer for Genesis VGM (solves dense PCM timing)
#include "audio_stream_dac_prerender.h"  // AudioStream for pre-rendered DAC playback
#include "bluetooth_manager.h"  // ESP32 Bluetooth control
#include "ui/framework/event_manager.h"  // GUI Framework Phase 1: Event system
#include "queue_manager.h"  // Queue system for sequential playback
#include "ui/framework/status_bar_manager.h"  // Global status bar with "Now playing:" and "Up Next:"

// --------- Config ----------
static const bool kForce2OpMode = false;          // Set true to disable 4-op voices (2-op only)
static const uint8_t kMax4OpVoices = 12;         // Max concurrent 4-op voices (1-12, each uses 2 channels)
bool g_drumSamplerEnabled = true;                 // Runtime toggle for PCM drum sampler (MIDI channel 10) - non-static for menu access
bool g_crossfeedEnabled = true;                   // Runtime toggle for stereo crossfeed (softer panning for MIDI) - non-static for menu access
bool g_reverbEnabled = true;                      // Runtime toggle for reverb effect (MIDI only) - non-static for menu access

// VGM-specific settings
uint8_t g_maxLoopsBeforeFade = 2;                 // 0 = loop forever, 1+ = fade after N loops (non-static for menu access)
float g_fadeDurationSeconds = 7.0f;               // Fade duration in seconds (non-static for menu access)
bool g_nesFiltersEnabled = false;                 // NES APU output filters (default OFF for raw sound)
bool g_nesStereoEnabled = true;                   // NES APU stereo panning (default ON)
bool g_spcFilterEnabled = false;                  // SPC gaussian filter (default OFF for raw sound)

// Genesis-specific settings
bool g_genesisDACEmulation = false;               // DAC emulation (OFF - using hardware DAC)

// Debug configuration (see debug_config.h to change settings)
#include "debug_config.h"
// ---------------------------

// CRITICAL: ALL objects MUST be pointers to prevent Teensy bricking!
// These will be created in setup() AFTER USB initialization

// Global pointers (using g_ prefix for consistency)
DisplayManager* displayManager = nullptr;
Adafruit_RGBLCDShield* lcd = nullptr;
LCDManager* g_lcdManager = nullptr;  // Smart LCD update manager
FileBrowser* browser = nullptr;
FloppyManager* floppy = nullptr;  // Local pointer (legacy, kept for compatibility)

// Global system objects
OPL3Synth* g_opl3 = nullptr;  // For silencing on stop
USBDriveManager* g_usbDrive = nullptr;  // For USB file browser
FloppyManager* g_floppy = nullptr;  // For floppy file browser
FileSource* g_fileSource = nullptr;  // File source abstraction
ScreenManager* g_screenManager = nullptr;  // For navigation tracking
DrumSamplerV2* g_drumSampler = nullptr;  // PCM drum sampler for MIDI channel 10
// CRITICAL FIX: Create NES APU, GB APU, and AudioStreamSPC as static stack objects, not heap-allocated
// The Audio Library's update graph doesn't reliably register heap objects created with new
static NESAPUEmulator g_nesAPU_obj;      // Stack object - constructor runs at startup, registers on update list
NESAPUEmulator* g_nesAPU = &g_nesAPU_obj; // Pointer to stack object for API compatibility

static GameBoyAPU g_gbAPU_obj;           // Stack object - constructor runs at startup, registers on update list
GameBoyAPU* g_gbAPU = &g_gbAPU_obj;      // Pointer to stack object for API compatibility

static AudioStreamSPC g_spc_obj;         // Stack object - constructor runs at startup, registers on update list
AudioStreamSPC* g_spcAudioStream = &g_spc_obj;  // Pointer to stack object for API compatibility

// DAC Pre-renderer (Genesis VGM PCM playback) - solves dense PCM timing issues
static AudioStreamDACPrerender g_dacPrerenderStream_obj;  // Stack object - constructor runs at startup
AudioStreamDACPrerender* g_dacPrerenderStream = &g_dacPrerenderStream_obj;  // Pointer for API compatibility
DACPrerenderer* g_dacPrerenderer = nullptr;  // Pre-renderer (heap allocated in setup)

// FM9 WAV player (embedded audio from FM9 extended VGM files)
// Uses custom AudioStream with sync support and PSRAM buffering
#include "audio_stream_fm9_wav.h"
static AudioStreamFM9Wav g_fm9WavStream_obj;  // Stack object for proper Audio Library registration
AudioStreamFM9Wav* g_fm9WavStream = &g_fm9WavStream_obj;  // Pointer for API compatibility

// FM9 MP3 player (embedded MP3 audio from FM9 extended VGM files)
// Uses Helix MP3 decoder with sync support and PSRAM buffering
#include "audio_stream_fm9_mp3.h"
static AudioStreamFM9Mp3 g_fm9Mp3Stream_obj;  // Stack object for proper Audio Library registration
AudioStreamFM9Mp3* g_fm9Mp3Stream = &g_fm9Mp3Stream_obj;  // Pointer for API compatibility

GenesisBoard* g_genesisBoard = nullptr;  // Genesis synthesizer board (external hardware)

PlayerManager* g_playerManager = nullptr;  // Unified player management (replaces PlayerFactory + PlaybackController + individual players)
PlaybackCoordinator* g_coordinator = nullptr;  // Event-driven playback lifecycle coordinator
QueueManager* g_queueManager = nullptr;  // Queue system for sequential playback (history + upcoming tracks)
BluetoothManager* g_bluetoothManager = nullptr;  // ESP32 Bluetooth control
EventManager* g_eventManager = nullptr;  // GUI Framework Phase 1: Event system

// USB Host MUST be global to initialize before main() for proper enumeration
USBHost g_myusb;
USBHub g_hub1(g_myusb);
USBDrive g_msDrive1(g_myusb);
USBFilesystem g_myFS(g_myusb);

// Audio objects - these are lightweight and safe as globals
AudioInputI2S            i2sIn;
AudioAnalyzePeak         peakLeft;    // Monitor left input level
AudioAnalyzePeak         peakRight;   // Monitor right input level
AudioMixer4              mixerLeft;
AudioMixer4              mixerRight;
AudioMixer4              mixerChannel1Left;   // Submixer for channel 1 (NES APU + SPC + GB APU)
AudioMixer4              mixerChannel1Right;  // Submixer for channel 1 (NES APU + SPC + GB APU)
AudioMixer4              dacNesMixerLeft;     // Pre-mixer for DAC Prerender + NES APU (fixes channel conflict)
AudioMixer4              dacNesMixerRight;    // Pre-mixer for DAC Prerender + NES APU (fixes channel conflict)
// Reverb removed to save ~50KB RAM (AudioEffectFreeverb uses 25KB each)
// AudioEffectFreeverb      reverbLeft;
// AudioEffectFreeverb      reverbRight;
AudioMixer4              finalMixerLeft;   // Now just passes through (reverb removed)
AudioMixer4              finalMixerRight;  // Now just passes through (reverb removed)
AudioMixer4              fadeMixerLeft;    // Final fade stage (affects both Bluetooth and line-out)
AudioMixer4              fadeMixerRight;   // Final fade stage (affects both Bluetooth and line-out)
AudioOutputI2S           i2sOut;
AudioControlSGTL5000     audioShield;

// Audio connections - These MUST remain global for the audio library
AudioConnection          patchCord1(i2sIn, 0, mixerLeft, 0);   // OPL3 left → mixerLeft ch0 (main)
AudioConnection          patchCord2(i2sIn, 1, mixerRight, 0);  // OPL3 right → mixerRight ch0 (main)
AudioConnection          patchCordPeakL(i2sIn, 0, peakLeft, 0);   // Monitor left input
AudioConnection          patchCordPeakR(i2sIn, 1, peakRight, 0);  // Monitor right input
// Connections to drum sampler will be created after initialization
AudioConnection*         patchCordDrumLeft = nullptr;
AudioConnection*         patchCordDrumRight = nullptr;

// ========== DAC/NES Pre-mixer Architecture ==========
// DAC Prerender and NES APU were previously both connected to submixer channel 0.
// In Teensy Audio Library, multiple sources to same mixer input = ONLY last one works!
// Fix: Route through a dedicated pre-mixer (dacNesMixer), then to submixer channel 0.
//
// Signal flow:
//   DAC Prerender ──→ dacNesMixer ch0 ──┐
//   NES APU ────────→ dacNesMixer ch1 ──┼──→ mixerChannel1 ch0 ──→ main mixer
//   (ch2 unused)      dacNesMixer ch2 ──┤
//   FM9 Audio ──────→ dacNesMixer ch3 ──┘
//
// Gain control: Players mute/unmute their respective dacNesMixer channels

// DAC Pre-render Stream (Genesis VGM PCM) → dacNesMixer channel 0
static AudioConnection   patchCordDACPrerenderLeft_obj(g_dacPrerenderStream_obj, 0, dacNesMixerLeft, 0);
static AudioConnection   patchCordDACPrerenderRight_obj(g_dacPrerenderStream_obj, 1, dacNesMixerRight, 0);
AudioConnection*         patchCordDACPrerenderLeft = &patchCordDACPrerenderLeft_obj;
AudioConnection*         patchCordDACPrerenderRight = &patchCordDACPrerenderRight_obj;

// NES APU → dacNesMixer channel 1
static AudioConnection   patchCordNESAPULeft_obj(g_nesAPU_obj, 0, dacNesMixerLeft, 1);
static AudioConnection   patchCordNESAPURight_obj(g_nesAPU_obj, 1, dacNesMixerRight, 1);
AudioConnection*         patchCordNESAPULeft = &patchCordNESAPULeft_obj;
AudioConnection*         patchCordNESAPURight = &patchCordNESAPURight_obj;

// ========== FM9 Audio Pre-mixer ==========
// FM9 files can have WAV or MP3 embedded audio (mutually exclusive)
// We need a small pre-mixer to combine these before feeding into dacNesMixer channel 3
// This avoids the "multiple connections to same channel" issue in Teensy Audio Library
AudioMixer4              fm9AudioMixerLeft;   // FM9 audio pre-mixer (WAV ch0, MP3 ch1)
AudioMixer4              fm9AudioMixerRight;  // FM9 audio pre-mixer (WAV ch0, MP3 ch1)

// FM9 WAV → fm9AudioMixer channel 0
static AudioConnection   patchCordFM9WavLeft_obj(g_fm9WavStream_obj, 0, fm9AudioMixerLeft, 0);
static AudioConnection   patchCordFM9WavRight_obj(g_fm9WavStream_obj, 1, fm9AudioMixerRight, 0);
AudioConnection*         patchCordFM9WavLeft = &patchCordFM9WavLeft_obj;
AudioConnection*         patchCordFM9WavRight = &patchCordFM9WavRight_obj;

// FM9 MP3 → fm9AudioMixer channel 1
static AudioConnection   patchCordFM9Mp3Left_obj(g_fm9Mp3Stream_obj, 0, fm9AudioMixerLeft, 1);
static AudioConnection   patchCordFM9Mp3Right_obj(g_fm9Mp3Stream_obj, 1, fm9AudioMixerRight, 1);
AudioConnection*         patchCordFM9Mp3Left = &patchCordFM9Mp3Left_obj;
AudioConnection*         patchCordFM9Mp3Right = &patchCordFM9Mp3Right_obj;

// FM9 pre-mixer output → dacNesMixer channel 3
static AudioConnection   patchCordFM9MixLeft_obj(fm9AudioMixerLeft, 0, dacNesMixerLeft, 3);
static AudioConnection   patchCordFM9MixRight_obj(fm9AudioMixerRight, 0, dacNesMixerRight, 3);

// DAC/NES Pre-mixer output → submixer channel 0
static AudioConnection   patchCordDacNesMixLeft_obj(dacNesMixerLeft, 0, mixerChannel1Left, 0);
static AudioConnection   patchCordDacNesMixRight_obj(dacNesMixerRight, 0, mixerChannel1Right, 0);

// SPC → submixer channel 1
static AudioConnection   patchCordSPCLeft_obj(g_spc_obj, 0, mixerChannel1Left, 1);
static AudioConnection   patchCordSPCRight_obj(g_spc_obj, 1, mixerChannel1Right, 1);
AudioConnection*         patchCordSPCLeft = &patchCordSPCLeft_obj;
AudioConnection*         patchCordSPCRight = &patchCordSPCRight_obj;

// GB APU → submixer channel 2
static AudioConnection   patchCordGBAPULeft_obj(g_gbAPU_obj, 0, mixerChannel1Left, 2);
static AudioConnection   patchCordGBAPURight_obj(g_gbAPU_obj, 1, mixerChannel1Right, 2);
AudioConnection*         patchCordGBAPULeft = &patchCordGBAPULeft_obj;
AudioConnection*         patchCordGBAPURight = &patchCordGBAPURight_obj;

// Submixer output → main mixer channel 1
AudioConnection          patchCordSubmixL(mixerChannel1Left, 0, mixerLeft, 1);
AudioConnection          patchCordSubmixR(mixerChannel1Right, 0, mixerRight, 1);

// Crossfeed connections for softer stereo panning (MIDI only) - back on channel 3
AudioConnection          patchCordCrossfeedL(i2sIn, 1, mixerLeft, 3);   // OPL3 right → mixerLeft ch3 (crossfeed)
AudioConnection          patchCordCrossfeedR(i2sIn, 0, mixerRight, 3);  // OPL3 left → mixerRight ch3 (crossfeed)
// Dry signal path (direct) - goes straight to final mixer (reverb removed)
AudioConnection          patchCord5(mixerLeft, 0, finalMixerLeft, 0);
AudioConnection          patchCord6(mixerRight, 0, finalMixerRight, 0);
// Reverb removed to save ~50KB RAM (AudioEffectFreeverb uses 25KB each)
// AudioConnection          patchCord7(mixerLeft, 0, reverbLeft, 0);
// AudioConnection          patchCord8(mixerRight, 0, reverbRight, 0);
// AudioConnection          patchCord9(reverbLeft, 0, finalMixerLeft, 1);
// AudioConnection          patchCord10(reverbRight, 0, finalMixerRight, 1);
// Fade stage (for VGM loop fadeout - affects both Bluetooth and line-out)
AudioConnection          patchCord11(finalMixerLeft, 0, fadeMixerLeft, 0);
AudioConnection          patchCord12(finalMixerRight, 0, fadeMixerRight, 0);
// Final output
AudioConnection          patchCord13(fadeMixerLeft, 0, i2sOut, 0);
AudioConnection          patchCord14(fadeMixerRight, 0, i2sOut, 1);

/**
 * @brief Test function for direct Genesis hardware validation
 *
 * This bypasses VGM playback entirely and directly programs the YM2612 and PSG
 * to produce simple test tones. Use this to verify that the Genesis board hardware,
 * timing, and register writes are working correctly before debugging VGM playback.
 *
 * Usage: Call from Serial menu or temporarily from setup()
 */
void testGenesisTone() {
  if (!g_genesisBoard) {
    Serial.println("ERROR: Genesis board not initialized!");
    return;
  }

  Serial.println("\n=== Genesis Hardware Test ===");
  Serial.println("Testing direct YM2612 + PSG register writes...");

  // CRITICAL: Unmute Teensy Audio Board line input AND fade mixer
  // Genesis board outputs analog audio that must be mixed with OPL3
  // The Teensy Audio Board (SGTL5000) needs line input enabled to hear it
  extern AudioControlSGTL5000 audioShield;
  extern AudioMixer4 fadeMixerLeft;
  extern AudioMixer4 fadeMixerRight;

  audioShield.inputSelect(AUDIO_INPUT_LINEIN);  // Switch from MIC to LINE IN FIRST
  audioShield.lineInLevel(0);  // 0-15 (0=minimum gain/quietest, 15=maximum gain/loudest), set AFTER selecting input
  audioShield.volume(0.8);  // Set headphone volume

  // CRITICAL: Unmute fade mixer (AudioSystem::initialize() mutes it by default)
  fadeMixerLeft.gain(0, 1.0f);   // Unmute left channel
  fadeMixerRight.gain(0, 1.0f);  // Unmute right channel

  Serial.println("Audio routing configured: LINE IN level 3, volume 80%, fade mixer unmuted");
  Serial.println("(Genesis analog output should be connected to Teensy line input)");

  Serial.println("\nWaiting 3 seconds - listen for any background noise BEFORE test tone...");
  delay(3000);
  Serial.println("If you heard constant noise, it's electrical interference, not the YM2612");

  GenesisBoard& gb = *g_genesisBoard;

  // Genesis board now uses smart timing - no mode switching needed
  Serial.println("Genesis board uses smart timing (automatic delays)");

  // Reset to clean state
  gb.reset();
  delay(50);

  Serial.println("\n1. Testing YM2612 FM tone (channel 0, algorithm 7 = pure carrier)...");

  // YM2612 minimal patch for a simple sine wave tone
  // Using channel 0 (port 0), operator 4 (carrier in algorithm 7)

  // CRITICAL: Silence EVERYTHING first to eliminate noise

  // Disable DAC mode (ensure channel 6 is in FM mode, not PCM)
  gb.writeYM2612(0, 0x2B, 0x00);  // DAC off

  // Key off ALL channels (0-5) on both ports
  for (uint8_t ch = 0; ch < 6; ch++) {
    gb.writeYM2612(0, 0x28, ch);  // Key off channel ch
  }

  // Silence ALL operators on channel 0 (set TL=127 = max attenuation)
  // Operator offsets: op1=0, op2=8, op3=4, op4=12
  for (uint8_t op = 0; op < 4; op++) {
    uint8_t offset = (op == 0) ? 0 : (op == 1) ? 8 : (op == 2) ? 4 : 12;
    gb.writeYM2612(0, 0x40 + offset, 0x7F);  // TL=127 (silence)
  }

  // Ensure PSG is completely silent
  gb.writePSG(0x9F);  // Channel 0 volume off
  gb.writePSG(0xBF);  // Channel 1 volume off
  gb.writePSG(0xDF);  // Channel 2 volume off
  gb.writePSG(0xFF);  // Noise channel volume off

  delay(100);  // Let silence settle

  Serial.println("   All channels silenced");

  // Global registers
  gb.writeYM2612(0, 0x22, 0x00);  // LFO off
  gb.writeYM2612(0, 0x27, 0x00);  // No timer, CH3 normal mode

  // Channel 0 algorithm and feedback (use algorithm 7 = all carriers, no modulation)
  gb.writeYM2612(0, 0xB0, 0x07);  // Feedback=0, Algorithm=7 (all ops → out, no FM)

  // Operator 4 (carrier in algorithm 7) - channel 0 = base offset 0, op4 = +12
  gb.writeYM2612(0, 0x30 + 12, 0x71);  // DT1=0, MUL=1 (1x frequency multiplier)
  gb.writeYM2612(0, 0x40 + 12, 0x00);  // TL=0 (maximum volume - carrier must be loud!)
  gb.writeYM2612(0, 0x50 + 12, 0x1F);  // RS=0, AR=31 (instant attack)
  gb.writeYM2612(0, 0x60 + 12, 0x00);  // AM=0, D1R=0 (no decay)
  gb.writeYM2612(0, 0x70 + 12, 0x00);  // D2R=0 (no sustain decay)
  gb.writeYM2612(0, 0x80 + 12, 0x0F);  // D1L=0, RR=15 (slow release)
  gb.writeYM2612(0, 0x90 + 12, 0x00);  // SSG-EG off

  // Set frequency for channel 0 (A4 = 440Hz)
  // YM2612 formula: fnum = (144 * freq * 2^(20-block)) / clock
  // For 8.00MHz clock, 440Hz, block=4:
  // fnum = (144 * 440 * 2^16) / 8000000 = (144 * 440 * 65536) / 8000000 = 518.69 ≈ 0x207
  gb.writeYM2612(0, 0xA4, 0x22);  // Block=4 (bits 3-5), F-num high 3 bits = 0x07 (bits 0-2) → 0x22
  gb.writeYM2612(0, 0xA0, 0x07);  // F-num low 8 bits = 0x07 (total fnum = 0x207)

  // Pan both channels (L=1, R=1)
  gb.writeYM2612(0, 0xB4, 0xC0);

  delay(100);  // Let registers settle

  // Key on operator 4 only for channel 0
  gb.writeYM2612(0, 0x28, 0x80);  // 0x80 = op4 on (bit 7), channel 0

  Serial.println("   YM2612 channel 0 keyed ON (should hear 440Hz tone)");
  Serial.println("   Listening for 3 seconds...");
  delay(3000);

  // Key off
  gb.writeYM2612(0, 0x28, 0x00);
  Serial.println("   YM2612 channel 0 keyed OFF");

  delay(500);

  Serial.println("\n2. Testing PSG tone (square wave on channel 0)...");

  // PSG test: Generate 440Hz square wave on tone channel 0
  // PSG frequency = clock / (32 * N)
  // For 3.58MHz clock, 440Hz: N = 3580000 / (32 * 440) ≈ 254
  uint16_t psgTone = 254;

  gb.writePSG(0x80 | (psgTone & 0x0F));        // Tone channel 0, low 4 bits
  gb.writePSG((psgTone >> 4) & 0x3F);          // High 6 bits
  gb.writePSG(0x90 | 0x08);                     // Volume = 8 (mid volume, 0=max, 15=off)

  Serial.println("   PSG channel 0 enabled (should hear 440Hz square wave)");
  Serial.println("   Listening for 3 seconds...");
  delay(3000);

  // Silence PSG
  gb.writePSG(0x9F);  // Volume = 15 (off)
  Serial.println("   PSG channel 0 silenced");

  Serial.println("\n=== Genesis Hardware Test Complete ===");
  Serial.println("If you heard both tones, the hardware is working correctly!");
  Serial.println("If not, check:");
  Serial.println("  - Pin connections (see genesis_board.h Config)");
  Serial.println("  - SN76489 clock source selection (board jumper H1)");
  Serial.println("  - Timing delays (genesis_board.h constants)");
  Serial.println("  - Serial debug output for register write confirmation\n");
}

void setup() {
  // ========================================
  // Initialize all hardware using HardwareInitializer
  // ========================================
  HardwareInitializer::Config hwConfig;
  hwConfig.max4OpVoices = kMax4OpVoices;
  hwConfig.force2OpMode = kForce2OpMode;
  hwConfig.showSplashScreen = false;  // Don't show "Ready!", let screen manager handle display

  HardwareInitializer::InitResult hwResult = HardwareInitializer::initializeAll(hwConfig);

  if (!hwResult.success) {
    // Critical error - halt system
    while (1) {
      delay(1000);
    }
  }

  // Store initialized hardware objects
  displayManager = hwResult.displayManager;
  lcd = hwResult.lcd;
  g_opl3 = hwResult.opl3;
  browser = hwResult.browser;
  floppy = hwResult.floppy;
  g_floppy = floppy;  // Also assign to global pointer for GUI access

  // Give floppy manager time to complete initialization handshake
  // FloppyManager sends STATUS commands and waits for "OK"/"FDC-USB Ready" response
  // This typically completes within 1-3 seconds
  if (floppy) {
    // // Serial.println("Waiting for floppy controller initialization...");
    uint32_t floppyInitStart = millis();
    while (millis() - floppyInitStart < 3000) {  // Wait up to 3 seconds
      floppy->update();
      if (floppy->isFloppyConnected()) {
        // // Serial.println("Floppy controller ready!");
        break;
      }
      delay(10);
    }
    if (!floppy->isFloppyConnected()) {
      // // Serial.println("Floppy controller not detected");
    }
  }

  // Note: ScreenManager initialization deferred until after all dependencies created

  // ========================================
  // Initialize Event System (GUI Framework Phase 1)
  // ========================================
  g_eventManager = new EventManager();
  // // Serial.println("[EventManager] Initialized");

  // ========================================
  // Initialize USB Host EARLY (before Audio Library)
  // ========================================
  // CRITICAL: Initialize USB Host before AudioMemory() to avoid DMA/interrupt conflicts
  // The Audio Library uses DMA channels and interrupts that can interfere with USB enumeration
  Serial.println("[Main] Initializing USB Host (before Audio Library)...");
  g_usbDrive = HardwareInitializer::initializeUSBHost(browser, g_myusb, g_hub1, g_msDrive1, g_myFS);

  // Wire USB callbacks to EventManager (fire events on connect/disconnect)
  if (g_usbDrive && g_eventManager) {
    g_usbDrive->setOnConnected([]() {
      Serial.println("[Main] USB drive connected - firing EVENT_USB_CONNECTED");
      g_eventManager->fire(EventManager::EVENT_USB_CONNECTED);
    });

    g_usbDrive->setOnDisconnected([]() {
      Serial.println("[Main] USB drive disconnected - firing EVENT_USB_DISCONNECTED");
      g_eventManager->fire(EventManager::EVENT_USB_DISCONNECTED);
    });

    Serial.println("[Main] USB callbacks wired to EventManager");
  }

  // ========================================
  // Create AudioStream objects BEFORE AudioMemory()
  // ========================================
  // CRITICAL: AudioStream objects must exist BEFORE AudioMemory() is called
  // AudioMemory() starts the Audio Library update system and builds the update graph
  // Objects created after AudioMemory() might not be included in the update list

  Serial.println("[Main] Creating AudioStream objects (must be before AudioMemory)");

  // SPC AudioStream already created as static stack object g_spc_obj
  Serial.println("[Main] AudioStreamSPC already exists as stack object");

  // NES APU already created as static stack object g_nesAPU_obj
  Serial.println("[Main] NESAPUEmulator already exists as stack object");

  // ========================================
  // Initialize audio system
  // ========================================
  // Allocate audio memory (starts the Audio Library update system)
  // 8 drum voices × ~4 buffers + overhead = 60 blocks
  AudioMemory(60);

  AudioSystem::Config audioConfig;
  audioConfig.masterVolume = 0.7f;
  audioConfig.opl3Gain = 0.8f;
  audioConfig.pcmGain = 0.0f;     // FM90S PCM initially silent
  audioConfig.drumGain = 0.4f;    // Drum sampler gain
  // CRITICAL: Always start with effects OFF (independent of user preference)
  // MIDI player will enable them during play() if user preference is true
  audioConfig.enableCrossfeed = false;  // Hardware starts OFF
  audioConfig.enableReverb = false;     // Hardware starts OFF

  // Reverb removed to save ~50KB RAM - simplified AudioSystem::initialize call
  AudioSystem::initialize(
    audioConfig,
    audioShield,
    mixerLeft, mixerRight,
    finalMixerLeft, finalMixerRight,
    fadeMixerLeft, fadeMixerRight
  );

  // ========================================
  // Create all the system objects (players, etc.)
  // ========================================
  g_fileSource = new FileSource();
  g_fileSource->setSource(FileSource::SD_CARD);

  // Initialize drum sampler if enabled
  if (g_drumSamplerEnabled) {
    g_drumSampler = new DrumSamplerV2();
    g_drumSampler->setEnabled(true);

    if (g_drumSampler->begin()) {
      // Create audio connections: drum sampler (stereo) -> mixerLeft and mixerRight
      // Drum sampler outputs go to mixer channel 2 (0=OPL3, 1=FM90S, 2=Drums)
      patchCordDrumLeft = new AudioConnection(g_drumSampler->getOutputLeft(), 0, mixerLeft, 2);
      patchCordDrumRight = new AudioConnection(g_drumSampler->getOutputRight(), 0, mixerRight, 2);

      // Set drum mixer gain (adjust to balance with OPL3)
      mixerLeft.gain(2, 0.40f);   // Drums at 40%
      mixerRight.gain(2, 0.40f);  // Drums at 40%

      // Tell OPL3 synth that drums are handled by sampler (frees 6 FM channels for melodic use)
      g_opl3->setDrumSamplerEnabled(true);

      // // Serial.println("PCM Drum Sampler: ENABLED");
    } else {
      // // Serial.println("WARNING: Drum sampler initialization failed!");
      delete g_drumSampler;
      g_drumSampler = nullptr;
      g_drumSamplerEnabled = false;
      // Drum sampler failed, use FM drums
      g_opl3->setDrumSamplerEnabled(false);
    }
  } else {
    // Drum sampler disabled, use FM drums (reserves 6 FM channels for drums)
    g_opl3->setDrumSamplerEnabled(false);
    // // Serial.println("PCM Drum Sampler: DISABLED (using FM drums)");
  }

  // ========================================
  // Connect NES APU Emulator (already created before AudioMemory)
  // ========================================
  // NES APU is used by VGMPlayer for NES/Famicom VGM files

  // ========== Initialize FM9 Audio Pre-mixer ==========
  // This pre-mixer combines FM9 WAV and MP3 streams (mutually exclusive)
  // Both channels start muted, FM9Player will unmute the appropriate one when playing
  fm9AudioMixerLeft.gain(0, 0.0f);   // FM9 WAV muted
  fm9AudioMixerLeft.gain(1, 0.0f);   // FM9 MP3 muted
  fm9AudioMixerLeft.gain(2, 0.0f);   // Unused
  fm9AudioMixerLeft.gain(3, 0.0f);   // Unused
  fm9AudioMixerRight.gain(0, 0.0f);  // FM9 WAV muted
  fm9AudioMixerRight.gain(1, 0.0f);  // FM9 MP3 muted
  fm9AudioMixerRight.gain(2, 0.0f);  // Unused
  fm9AudioMixerRight.gain(3, 0.0f);  // Unused
  Serial.println("[Main] FM9 Audio Pre-mixer initialized (WAV/MP3 muted)");

  // ========== Initialize DAC/NES Pre-mixer ==========
  // This pre-mixer combines DAC Prerender, NES APU, and FM9 audio before feeding into submixer channel 0
  // All channels start muted, players will unmute the appropriate one when playing
  dacNesMixerLeft.gain(0, 0.0f);   // DAC Prerender muted
  dacNesMixerLeft.gain(1, 0.0f);   // NES APU muted
  dacNesMixerLeft.gain(2, 0.0f);   // Unused
  dacNesMixerLeft.gain(3, 0.0f);   // FM9 audio pre-mixer muted
  dacNesMixerRight.gain(0, 0.0f);  // DAC Prerender muted
  dacNesMixerRight.gain(1, 0.0f);  // NES APU muted
  dacNesMixerRight.gain(2, 0.0f);  // Unused
  dacNesMixerRight.gain(3, 0.0f);  // FM9 audio pre-mixer muted
  Serial.println("[Main] DAC/NES Pre-mixer initialized (all channels muted)");

  // ========== Initialize Channel 1 Submixer ==========
  // Channel 0: DAC/NES pre-mixer output (unity gain - individual control via dacNesMixer)
  // Channel 1: SPC
  // Channel 2: GB APU
  // Channel 3: Unused
  // All inputs start muted except channel 0 which passes through from dacNesMixer
  mixerChannel1Left.gain(0, 1.0f);   // DAC/NES pre-mixer at unity (individual muting via dacNesMixer)
  mixerChannel1Left.gain(1, 0.0f);   // SPC muted
  mixerChannel1Left.gain(2, 0.0f);   // GB APU muted
  mixerChannel1Left.gain(3, 0.0f);   // Unused
  mixerChannel1Right.gain(0, 1.0f);  // DAC/NES pre-mixer at unity
  mixerChannel1Right.gain(1, 0.0f);  // SPC muted
  mixerChannel1Right.gain(2, 0.0f);  // GB APU muted
  mixerChannel1Right.gain(3, 0.0f);  // Unused

  // Main mixer channel 1 set to unity gain (submixer controls individual volumes)
  mixerLeft.gain(1, 1.0f);   // Channel 1 at unity gain
  mixerRight.gain(1, 1.0f);  // Channel 1 at unity gain

  Serial.println("[Main] Channel 1 submixer initialized (SPC/GB muted, DAC/NES pre-mixer at unity)");

  // ========================================
  // SPC AudioStream connections already created as static objects
  // ========================================
  // SPC is used by SPCPlayer for SNES music files
  // Connections patchCordSPCLeft_obj and patchCordSPCRight_obj were created
  // as static stack objects at file scope

  // Start with SPC muted (SPCPlayer will unmute when playing)
  // Note: Mixer channel 1 already muted from APU setup above, both share the same channel

  Serial.println("[Main] SPC AudioStream connections already exist as stack objects (muted)");

  // ========================================
  // Initialize DAC Pre-renderer (Genesis VGM PCM playback)
  // ========================================
  // DAC Pre-renderer expands all DAC commands to a linear 44.1 kHz sample stream
  // before playback begins. This solves timing issues with dense PCM that
  // real-time emulation cannot handle (e.g., streaming PCM at 44.1 kHz rate).
  g_dacPrerenderer = new DACPrerenderer();

  // DAC prerender stream connections already created as static objects
  // Connected to submixer channel 0 (shares with NES APU - only one plays at a time)
  // Start muted, VGMPlayer will unmute when Genesis VGM with DAC plays
  // Note: Channel 0 already muted from setup above

  Serial.println("[Main] DAC Pre-renderer initialized");

  // ========================================
  // Initialize Genesis Board (if connected)
  // ========================================
  // Genesis board is external hardware for Sega Genesis VGM playback
  // YM2612 (FM) + SN76489 (PSG) chips
  g_genesisBoard = new GenesisBoard();

  GenesisBoard::Config genesisConfig = {
    .pinWrSN = 41,  // SN76489 write strobe (active low)
    .pinWrYM = 34,  // YM2612 write strobe (active low)
    .pinIcYM = 35,  // YM2612 reset (active low)
    .pinA0YM = 36,  // YM2612 A0 (address/data select)
    .pinA1YM = 37,  // YM2612 A1 (port select)
    .pinSCK = 38,   // SPI clock for shift register
    .pinSDI = 40    // SPI data (MOSI) for shift register
    // Clocks generated on board: SN76489 @ 3.579545 MHz, YM2612 @ 7.68 MHz
  };

  g_genesisBoard->begin(genesisConfig);
  Serial.println("[Main] Genesis board initialized (YM2612 + SN76489)");
  Serial.println("  Note: Genesis audio outputs through analog AOUT pin");
  Serial.println("  Connect to line input along with OPL3 via passive mixer");

  // Disable full debug mode (too verbose)
  // g_genesisBoard->setDebugMode(true);
  // Serial.println("[Main] Genesis board debug mode ENABLED - will print all register writes");

  // ========================================
  // GENESIS HARDWARE TEST (TEMPORARY - ENABLED)
  // ========================================
  // Uncomment this to run direct hardware validation test
  // This bypasses VGM playback and directly programs YM2612 + PSG
  // Comment out when done testing
  // testGenesisTone();  // <-- DISABLED
  // ========================================

  // ========================================
  // Create PlayerManager (unified player management)
  // ========================================
  // PlayerManager replaces:
  // - PlayerFactory (for creating players)
  // - PlaybackController (for updating players)
  // - Individual player globals (players created on-demand)
  //
  // Benefits:
  // - Only one player exists at a time (saves RAM)
  // - Centralized audio routing (no duplication)
  // - PlaybackState synchronization built-in
  // - Event firing (PLAYBACK_STARTED/STOPPED) built-in
  //
  // Note: PlayerManager creation must happen BEFORE ScreenContext
  // so we can pass it to screens that need to start playback

  PlayerConfig playerConfig;
  playerConfig.opl3 = g_opl3;
  playerConfig.fileSource = g_fileSource;
  playerConfig.drumSampler = g_drumSampler;
  playerConfig.nesAPU = g_nesAPU;  // Global NES APU emulator (stays alive)
  playerConfig.gbAPU = g_gbAPU;    // Global Game Boy APU emulator (stays alive)
  playerConfig.genesisBoard = g_genesisBoard;  // Genesis synthesizer board (YM2612 + SN76489)
  playerConfig.dacPrerenderer = g_dacPrerenderer;  // DAC pre-renderer for Genesis VGM PCM playback
  playerConfig.dacPrerenderStream = g_dacPrerenderStream;  // Pre-rendered DAC playback stream
  playerConfig.spcAudioStream = g_spcAudioStream;  // Global SPC audio stream (stays alive)
  playerConfig.mixerLeft = &mixerLeft;
  playerConfig.mixerRight = &mixerRight;
  playerConfig.mixerChannel1Left = &mixerChannel1Left;
  playerConfig.mixerChannel1Right = &mixerChannel1Right;
  playerConfig.dacNesMixerLeft = &dacNesMixerLeft;    // DAC/NES pre-mixer for VGMPlayer
  playerConfig.dacNesMixerRight = &dacNesMixerRight;  // DAC/NES pre-mixer for VGMPlayer
  playerConfig.fm9AudioMixerLeft = &fm9AudioMixerLeft;    // FM9 WAV/MP3 pre-mixer for FM9Player
  playerConfig.fm9AudioMixerRight = &fm9AudioMixerRight;  // FM9 WAV/MP3 pre-mixer for FM9Player
  playerConfig.finalMixerLeft = &finalMixerLeft;
  playerConfig.finalMixerRight = &finalMixerRight;
  playerConfig.fadeMixerLeft = &fadeMixerLeft;
  playerConfig.fadeMixerRight = &fadeMixerRight;
  // Reverb removed to save ~50KB RAM
  playerConfig.reverbLeft = nullptr;
  playerConfig.reverbRight = nullptr;
  playerConfig.crossfeedEnabled = g_crossfeedEnabled;
  playerConfig.reverbEnabled = false;  // Reverb removed

  g_playerManager = new PlayerManager(playerConfig);

  // // Serial.println("[Main] PlayerManager created");
  // Note: EventManager will be set later after it's initialized

  // ========================================
  // Create QueueManager (queue system for sequential playback)
  // ========================================
  g_queueManager = new QueueManager();
  g_queueManager->setEventManager(g_eventManager);
  Serial.println("[Main] QueueManager created and wired to EventManager");

  // ========================================
  // Create PlaybackCoordinator (event-driven lifecycle)
  // ========================================
  // PlaybackCoordinator orchestrates playback operations:
  // - requestPlay() → load file → wait for screen → start playback
  // - requestStop() → stop playback → wait for cleanup → fire complete event
  //
  // Note: Callbacks will be wired after creation
  g_coordinator = new PlaybackCoordinator(g_playerManager, g_eventManager, PlaybackState::getInstance(), g_queueManager);
  Serial.println("[Main] PlaybackCoordinator created with QueueManager");

  // USB drive manager already initialized early (before Audio Library) to avoid conflicts

  // ========================================
  // Initialize ESP32 Bluetooth control (Serial3)
  // ========================================
  Serial3.begin(115200);
  g_bluetoothManager = new BluetoothManager();
  g_bluetoothManager->begin();

  // Wire EventManager to Bluetooth manager (GUI Framework Phase 1)
  if (g_bluetoothManager && g_eventManager) {
    g_bluetoothManager->setEventManager(g_eventManager);
    // // Serial.println("[EventManager] Wired to BluetoothManager");
  }

  // Initialize Bluetooth
  g_bluetoothManager->initialize();

  // ========================================
  // Initialize ScreenManager with ScreenContext (new framework pattern)
  // ========================================
  ScreenContext* screenContext = new ScreenContext();
  screenContext->ui = displayManager->getRetroUI();
  screenContext->lcd = lcd;
  screenContext->eventManager = g_eventManager;
  screenContext->opl3 = g_opl3;
  screenContext->fileSource = g_fileSource;
  screenContext->playbackState = PlaybackState::getInstance();
  screenContext->playerManager = g_playerManager;  // Unified player management
  screenContext->coordinator = g_coordinator;  // Event-driven playback lifecycle
  screenContext->queueManager = g_queueManager;  // Queue system for sequential playback

  // ========================================
  // Initialize StatusBarManager (Global status bar with "Now playing:" and "Up Next:")
  // ========================================
  StatusBarManager* g_statusBarManager = new StatusBarManager(
      displayManager->getRetroUI(),
      g_eventManager,
      PlaybackState::getInstance(),
      g_queueManager
  );
  g_statusBarManager->begin();  // Register event listeners
  screenContext->statusBarManager = g_statusBarManager;
  Serial.println("[StatusBarManager] Initialized with event-driven status updates");

  screenContext->usbDrive = g_usbDrive;
  screenContext->bluetooth = g_bluetoothManager;
  screenContext->floppy = g_floppy;

  // ========================================
  // Initialize LCDManager (Time-sliced LCD updates - ZERO blocking!)
  // ========================================
  g_lcdManager = new LCDManager(lcd);
  screenContext->lcdManager = g_lcdManager;
  Serial.println("[LCDManager] Initialized with time-sliced updates (1 char per iteration, 3ms spacing)");

  // Create ScreenManager and assign it to context (circular reference)
  g_screenManager = ScreenManager::getInstance();
  screenContext->screenManager = g_screenManager;

  // Initialize ScreenManager with context
  g_screenManager->init(screenContext);

  // Start with main menu
  g_screenManager->switchTo(SCREEN_MAIN_MENU);

  // ========================================
  // Initialize SystemEventHandlers
  // ========================================
  USBEventHandler::initialize(screenContext, g_screenManager);
  PlaybackEventHandler::initialize(screenContext, g_screenManager);
  AudioEventHandler::initialize(screenContext);

  // Initialize PlaybackNavigationHandler (Phase 5)
  // Handles navigation decisions based on coordinator events
  PlaybackNavigationHandler::initialize(screenContext, g_screenManager, g_coordinator);

  // ========================================
  // Wire PlayerManager natural completion callback to PlaybackCoordinator (Phase 5)
  // ========================================
  // Note: Load/Start/Stop callbacks are passed directly to async methods by coordinator
  // Only natural completion needs to be registered here (called from PlayerManager::update())

  // Natural completion callback: Song ended naturally
  g_playerManager->setNaturalCompletionCallback([]() {
    if (g_coordinator) {
      g_coordinator->onNaturalCompletion();
    }
  });

  // // Serial.println("[Main] PlayerManager natural completion callback wired to PlaybackCoordinator");

  // Create menu system (legacy serial interface)
  // TODO Phase 6: MenuSystem still references individual players - will be updated or removed
  // menu = new MenuSystem(g_midiPlayer, g_vgmPlayer, g_droPlayer, g_imfPlayer, g_radPlayer, g_spcPlayer,
  //                       browser, floppy, g_usbDrive, g_fileSource);
  // menu->begin();
  // // Serial.println("[Main] MenuSystem temporarily disabled during PlayerManager migration");
}

void loop() {
  // Debug: Check if SPC AudioStream is getting updates (temporary)
  static unsigned long lastSPCCheck = 0;
  static int spcCheckCount = 0;
  unsigned long now = millis();
  if (now - lastSPCCheck > 1000 && spcCheckCount < 10) {
    lastSPCCheck = now;
    spcCheckCount++;
    if (g_spcAudioStream) {
      Serial.printf("[Main] SPC AudioStream diagnostics after %d seconds:\n", spcCheckCount);
      Serial.printf("  - updateCount: %lu\n", g_spcAudioStream->getUpdateCount());
      Serial.printf("  - ticks (volatile): %lu\n", g_spcAudioStream->getTicks());
      Serial.printf("  - Expected ~344 ticks/sec if update() is being called\n");
    }
  }

  // Monitor input levels for clipping detection (print every 3 seconds)
  static unsigned long lastPeakCheck = 0;
  if (now - lastPeakCheck > 3000) {
    lastPeakCheck = now;
    if (peakLeft.available() && peakRight.available()) {
      float pkL = peakLeft.read();
      float pkR = peakRight.read();

      // Peak values: 0.0 to 1.0 range (1.0 = clipping)
      if (pkL > 0.1 || pkR > 0.1) {  // Only print if there's actual signal
        Serial.printf("[Audio] Input peaks: L=%.2f R=%.2f ", pkL, pkR);
        if (pkL > 0.95 || pkR > 0.95) {
          Serial.println("*** CLIPPING! ***");
        } else if (pkL > 0.8 || pkR > 0.8) {
          Serial.println("(high - near clipping)");
        } else {
          Serial.println("(OK)");
        }
      }
    }
  }

  // Update playback (player updates, progress tracking, completion detection)
  // PlayerManager handles:
  // - Calling player->update()
  // - Syncing PlaybackState
  // - Detecting completion
  // - Firing events
  // - Auto-navigation
  g_playerManager->update();

  // CRITICAL: Update drum sampler voice cleanup EVERY loop iteration
  // This must run constantly to free voices as samples finish playing
  // If only called during MIDI event processing, voices leak and accumulate
  if (g_drumSampler) {
    g_drumSampler->update();
  }

  // CRITICAL: Refill DAC pre-render buffer from SD card
  // This MUST be called from main loop (not ISR) to safely read from SD card
  // The ISR reads from the ring buffer, main loop refills it from file
  if (g_dacPrerenderStream && g_dacPrerenderStream->needsRefill()) {
    g_dacPrerenderStream->refillBuffer();
  }

  // Update USB drive manager (hot-plug detection)
  // Calls myusb.Task() and fires callbacks when drive connects/disconnects
  if (g_usbDrive) {
    g_usbDrive->update();
  }

  // Update Bluetooth manager (process ESP32 responses)
  if (g_bluetoothManager) {
    g_bluetoothManager->update();
  }

  // Update screen navigation system (GUI)
  if (g_screenManager) {
    g_screenManager->update();
  }

  // Update LCD manager (smart LCD updates with throttling and dirty checking)
  // Call this AFTER screen manager update() so screens can set LCD content
  if (g_lcdManager) {
    g_lcdManager->update();
  }

  // Serial menu system removed - GUI only now

  // Yield to background tasks (non-blocking, allows USB enumeration, etc.)
  yield();
}