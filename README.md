# FM-90s Hardware Music Player

A hardware-based retro music player ecosystem built around the Teensy 4.1. Play classic video game and computer music formats using **real FM synthesis chips** - not emulation.

The FM-90s plays music exactly as it was meant to sound: through actual Yamaha OPL3 chips, Sega Genesis hardware, and other authentic sound generators. Optional expansion boards add support for more systems while a companion converter tool handles 100+ source formats.

## Why Hardware?

Software emulation gets close, but real chips have character. The warmth of an OPL3, the punch of a YM2612, the crunch of a SN76489 - these come from analog circuitry, component tolerances, and quirks that emulators approximate but never quite capture.

The FM-90s gives you that authentic sound in a modern, easy-to-use package.

## Supported Formats

**Native Playback:**
- **MIDI** (.mid, .midi, .smf, .kar) - General MIDI with 128 FM instruments
- **VGM** (.vgm, .vgz) - Video Game Music (OPL2/OPL3, NES APU, Game Boy APU, Genesis)
- **FM9** (.fm9) - Extended VGM with synchronized audio and cover art
- **SPC** (.spc) - SNES SPC700 sound files

**Via fmconv Converter (100+ formats):**
- MIDI variants: XMI, MUS (DOOM), HMP/HMI (Descent), RMI, KAR
- DOS/AdLib: RAD, DRO, IMF, CMF, A2M, HSC, LAA, and 30+ more
- Trackers: MOD, XM, S3M, IT, MPTM, and 60+ variants

The [fmconv](https://github.com/yourusername/fmconv) converter runs on your PC and transforms these formats into optimized FM9 files for the player.

## Hardware Ecosystem

### Core System
- **Teensy 4.1** - 600MHz ARM Cortex-M7 brain
- **Dual OPL3 Board** - Two YMF262 chips providing 36 FM voices
- **Teensy Audio Board** - High-quality DAC with headphone amp
- **Display System** - 800x480 TFT + 16x2 LCD with navigation buttons

### Expansion Boards

| Board | Sound Hardware | Formats |
|-------|---------------|---------|
| **Genesis Engine** | YM2612 + SN76489 | Sega Genesis/Mega Drive VGM |
| **PC-Tone Module 4** | SAA1099 × 2 | PC speaker / Tandy / CMS |
| **Bluetooth Module** | ESP32 | Wireless audio streaming |
| **Floppy Interface** | Arduino Nano | 3.5" floppy disk support |

All expansion boards are optional - the core system plays the full format library.

## Features

### Audio
- **Real FM Synthesis** - Dual YMF262 chips, not emulation
- **4-Op Voices** - Up to 12 concurrent 4-op instruments for rich sounds
- **PCM Drum Sampler** - 8-voice polyphonic drums with PROGMEM samples
- **APU Emulation** - Software NES/Game Boy APU for chiptune VGMs
- **Audio Effects** - Crossfeed and reverb (MIDI), authentic low-pass filters

### Interface
- **Retro DOS-Style GUI** - 100×30 character grid with 16-color palette
- **Dual Display** - Album art and metadata on TFT, controls on LCD
- **File Browser** - SD card, USB drive, and floppy support
- **Playback Queue** - Build playlists, navigate history
- **Real-Time Visualization** - OPL register activity display

### Connectivity
- **SD Card** - Primary storage (FAT32)
- **USB Drive** - Hot-plug support with file transfer
- **Bluetooth** - Wireless audio to headphones/speakers
- **Floppy** - Because why not?

## Quick Start

1. **Assemble hardware** - Teensy 4.1 + OPL3 board + Audio board + displays
2. **Flash firmware** - `pio run -t upload`
3. **Copy music** - Drop .mid, .vgm, .fm9, .spc files on SD card
4. **Play** - Navigate with LCD buttons, enjoy authentic FM sound

For other formats, convert with fmconv first:
```bash
fmconv doom_e1m1.mus          # DOOM music → FM9
fmconv descent.hmp            # Descent → FM9
fmconv chiptune.rad           # Reality AdLib Tracker → FM9
```

## Building

### Prerequisites
- [PlatformIO](https://platformio.org/)
- Teensy 4.1 with USB cable

### Compile & Upload
```bash
# Windows
~/.platformio/penv/Scripts/pio run -t upload

# Linux/Mac
pio run -t upload
```

## Pin Assignments

<details>
<summary>Click to expand pin table</summary>

| Function | Pins |
|----------|------|
| **OPL3 Board** | 2 (A0), 3 (A1), 4 (A2), 5 (/IC), 6 (/WR), 11 (MOSI), 13 (SCK) |
| **TFT Display (SPI1)** | 26 (MOSI), 27 (SCK), 28 (CS), 29 (RST), 39 (MISO) |
| **LCD Shield (I2C)** | 18 (SDA), 19 (SCL) |
| **Audio Board (I2S)** | 7 (TX), 20 (LRCLK), 21 (BCLK), 23 (MCLK) |
| **Genesis Board** | 30-36 (directly to header) |
| **Bluetooth (Serial3)** | 14 (TX), 15 (RX) |
| **Floppy (Serial4)** | 16 (RX), 17 (TX) |

</details>

## The FM9 Format

FM9 is an extended VGM format designed for this player:

- **VGM Core** - Standard video game music register data
- **Embedded Audio** - Synchronized WAV/MP3 for hybrid playback
- **Cover Art** - 100×100 album artwork
- **Metadata** - Title, artist, game, system info

Created by fmconv from any supported source format.

## Credits

### Sound Chips & Patches
- **The Fat Man** (George Sanger) - General MIDI FM instrument patches
- **Yamaha** - YMF262 (OPL3), YM2612 FM synthesis chips
- **Texas Instruments** - SN76489 PSG

### Libraries & Tools
- **ArduinoOPL2** by DhrBaksteen - OPL2/OPL3 hardware interface
- **snes_spc** by blargg - SPC700 emulation
- **libADLMIDI** - MIDI to OPL synthesis
- **AdPlug** - Classic AdLib format support
- **OpenMPT** - Tracker format support

### Hardware
- **PJRC** - Teensy 4.1 development board
- **Adafruit** - RGB LCD Shield, display libraries

## Related Projects

- **[fmconv](https://github.com/yourusername/fmconv)** - Format converter (100+ formats → FM9)
- **[ESP32-BT-Audio](https://github.com/yourusername/esp32-bt-audio)** - Bluetooth module firmware

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

---

*The FM-90s: Real chips. Real sound. Real nostalgia.*
