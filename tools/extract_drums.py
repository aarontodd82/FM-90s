#!/usr/bin/env python3
"""
Extract drum samples from SoundFont (.sf2) files and convert to RAW format.
Generates C header files for embedding in Teensy firmware.

Requirements: pip install sf2utils numpy scipy

Usage: python extract_drums.py input.sf2 output_dir
"""

import sys
import os
import struct
import wave
import numpy as np
from pathlib import Path

try:
    from sf2utils.sf2parse import Sf2File
except ImportError:
    print("ERROR: sf2utils not installed")
    print("Install with: pip install sf2utils")
    sys.exit(1)

# General MIDI drum map (channel 10, notes 27-87)
GM_DRUM_MAP = {
    27: "High Q",
    28: "Slap",
    29: "Scratch Push",
    30: "Scratch Pull",
    31: "Sticks",
    32: "Square Click",
    33: "Metronome Click",
    34: "Metronome Bell",
    35: "Acoustic Bass Drum",
    36: "Bass Drum 1",
    37: "Side Stick",
    38: "Acoustic Snare",
    39: "Hand Clap",
    40: "Electric Snare",
    41: "Low Floor Tom",
    42: "Closed Hi-Hat",
    43: "High Floor Tom",
    44: "Pedal Hi-Hat",
    45: "Low Tom",
    46: "Open Hi-Hat",
    47: "Low-Mid Tom",
    48: "Hi-Mid Tom",
    49: "Crash Cymbal 1",
    50: "High Tom",
    51: "Ride Cymbal 1",
    52: "Chinese Cymbal",
    53: "Ride Bell",
    54: "Tambourine",
    55: "Splash Cymbal",
    56: "Cowbell",
    57: "Crash Cymbal 2",
    58: "Vibraslap",
    59: "Ride Cymbal 2",
    60: "Hi Bongo",
    61: "Low Bongo",
    62: "Mute Hi Conga",
    63: "Open Hi Conga",
    64: "Low Conga",
    65: "High Timbale",
    66: "Low Timbale",
    67: "High Agogo",
    68: "Low Agogo",
    69: "Cabasa",
    70: "Maracas",
    71: "Short Whistle",
    72: "Long Whistle",
    73: "Short Guiro",
    74: "Long Guiro",
    75: "Claves",
    76: "Hi Wood Block",
    77: "Low Wood Block",
    78: "Mute Cuica",
    79: "Open Cuica",
    80: "Mute Triangle",
    81: "Open Triangle",
    82: "Shaker",
    83: "Jingle Bell",
    84: "Bell Tree",
    85: "Castanets",
    86: "Mute Surdo",
    87: "Open Surdo",
}

# Priority drums to extract (most commonly used)
PRIORITY_DRUMS = [
    36,  # Bass Drum 1 (kick)
    38,  # Acoustic Snare
    42,  # Closed Hi-Hat
    46,  # Open Hi-Hat
    49,  # Crash Cymbal 1
    51,  # Ride Cymbal 1
    45,  # Low Tom
    47,  # Low-Mid Tom
    48,  # Hi-Mid Tom
    50,  # High Tom
    37,  # Side Stick (rimshot)
    39,  # Hand Clap
]

def trim_silence(audio_data, threshold=0.01):
    """Trim silence from start and end of audio."""
    # Find first non-silent sample
    abs_audio = np.abs(audio_data)
    max_val = np.max(abs_audio)
    threshold_val = max_val * threshold

    non_silent = np.where(abs_audio > threshold_val)[0]
    if len(non_silent) == 0:
        return audio_data

    start = max(0, non_silent[0] - 100)  # Keep 100 samples before
    end = min(len(audio_data), non_silent[-1] + 4410)  # Keep 0.1s after (44100/10)

    return audio_data[start:end]

def resample_audio(audio_data, orig_rate, target_rate=44100):
    """Resample audio to target rate."""
    if orig_rate == target_rate:
        return audio_data

    try:
        from scipy import signal
        # Calculate resampling ratio
        num_samples = int(len(audio_data) * target_rate / orig_rate)
        return signal.resample(audio_data, num_samples)
    except ImportError:
        print("  WARNING: scipy not installed, cannot resample. Install with: pip install scipy")
        return audio_data

def convert_to_mono(audio_data, num_channels):
    """Convert stereo to mono by averaging channels."""
    if num_channels == 1:
        return audio_data

    # Reshape to (samples, channels) and average
    audio_2d = audio_data.reshape(-1, num_channels)
    return np.mean(audio_2d, axis=1)

def normalize_audio(audio_data, target_peak=0.9):
    """Normalize audio to target peak level."""
    max_val = np.max(np.abs(audio_data))
    if max_val > 0:
        return audio_data * (target_peak / max_val)
    return audio_data

def extract_drums_from_sf2(sf2_path, output_dir):
    """Extract drum samples from SF2 file and convert to WAV."""
    print(f"\nExtracting drums from: {sf2_path}")

    # IMPORTANT: Keep the file open throughout extraction!
    sf2_file = open(sf2_path, 'rb')
    try:
        sf2 = Sf2File(sf2_file)

        # Find drum presets (typically bank 128 or preset name contains "drum")
        drum_presets = []
        for preset in sf2.presets:
            # Standard MIDI drum kit is usually bank=128, preset=0
            # Or preset name contains "drum", "kit", "percussion"

            # Get bank/preset numbers safely
            try:
                bank_num = preset.bank if hasattr(preset, 'bank') else 0
                preset_num = preset.preset if hasattr(preset, 'preset') else 0
                name = preset.name if isinstance(preset.name, str) else preset.name.decode('utf-8', errors='ignore').strip('\x00')
            except Exception as e:
                continue

            if (bank_num == 128 or
                'drum' in name.lower() or
                'kit' in name.lower() or
                'standard' in name.lower()):
                drum_presets.append(preset)
                print(f"  Found drum preset: {name} (bank={bank_num}, preset={preset_num})")

        if not drum_presets:
            print("  No drum presets found in SF2 file")
            return []

        # Use the first drum preset found (prefer "Standard" if available)
        drum_preset = drum_presets[0]
        for preset in drum_presets:
            try:
                name = preset.name if isinstance(preset.name, str) else preset.name.decode('utf-8', errors='ignore').strip('\x00')
                if 'standard' in name.lower():
                    drum_preset = preset
                    break
            except:
                pass

        preset_name = drum_preset.name if isinstance(drum_preset.name, str) else drum_preset.name.decode('utf-8', errors='ignore').strip('\x00')
        print(f"  Using preset: {preset_name}")

        # Get the instrument from the preset
        if not drum_preset.bags:
            print("  ERROR: Drum preset has no bags")
            return []

        instrument = drum_preset.bags[0].instrument
        if not instrument:
            print("  ERROR: Could not find instrument")
            return []

        print(f"  Instrument: {instrument.name}")
        print(f"  Instrument has {len(instrument.bags)} instrument bags")

        extracted_samples = []

        # Extract samples for each MIDI note in the drum range
        for note_num in range(27, 88):  # GM drum range
            drum_name = GM_DRUM_MAP.get(note_num, f"Note{note_num}")

            # Find the instrument bag for this note
            sample_obj = None
            for inst_bag in instrument.bags:
                key_range = inst_bag.key_range

                if key_range:
                    # key_range is a list [low, high]
                    if isinstance(key_range, list) and len(key_range) == 2:
                        low_key = key_range[0]
                        high_key = key_range[1]
                    else:
                        # Fallback for integer format
                        low_key = key_range & 0xFF
                        high_key = (key_range >> 8) & 0xFF

                    if low_key <= note_num <= high_key:
                        sample_obj = inst_bag.sample
                        break

            if sample_obj:
                try:
                    # Extract sample data
                    sample_data = sample_obj.raw_sample_data
                    sample_rate = sample_obj.sample_rate

                    # Convert sample data to numpy array
                    audio_data = np.frombuffer(sample_data, dtype=np.int16).astype(np.float32) / 32768.0

                    # Create safe filename
                    safe_name = drum_name.lower().replace(' ', '_').replace('-', '_')
                    safe_name = ''.join(c for c in safe_name if c.isalnum() or c == '_')

                    # Save as WAV first
                    wav_path = output_dir / f"{safe_name}_{note_num}.wav"

                    with wave.open(str(wav_path), 'wb') as wav_file:
                        wav_file.setnchannels(1)
                        wav_file.setsampwidth(2)
                        wav_file.setframerate(sample_rate)
                        wav_file.writeframes((audio_data * 32767).astype(np.int16).tobytes())

                    sample_name = sample_obj.name if isinstance(sample_obj.name, str) else sample_obj.name.decode('utf-8', errors='ignore').strip('\x00')
                    print(f"  Extracted note {note_num} ({drum_name}) -> {wav_path.name} [{sample_name}]")
                    extracted_samples.append({
                        'note': note_num,
                        'name': drum_name,
                        'wav_path': wav_path
                    })
                except Exception as e:
                    print(f"  ERROR extracting note {note_num}: {e}")

        return extracted_samples

    finally:
        sf2_file.close()

def save_as_raw(audio_data, output_path):
    """Save audio data as 16-bit signed RAW file."""
    # Convert float32 to int16
    audio_int16 = (audio_data * 32767).astype(np.int16)

    with open(output_path, 'wb') as f:
        f.write(audio_int16.tobytes())

    return len(audio_int16) * 2  # Return size in bytes

def generate_c_header(sample_name, raw_file_path, output_path):
    """Generate C header file from RAW audio file in AudioPlayMemory format."""
    with open(raw_file_path, 'rb') as f:
        data = f.read()

    # Convert to 16-bit samples
    num_samples = len(data) // 2
    samples = struct.unpack(f'<{num_samples}h', data)  # Little-endian signed 16-bit

    # AudioPlayMemory format: unsigned int array where:
    # - First element = number of samples
    # - Remaining elements = sample data (int16 packed into uint32, 2 samples per uint32)
    # Total array size = 1 + (num_samples + 1) / 2
    num_uint32 = 1 + (num_samples + 1) // 2

    with open(output_path, 'w') as f:
        f.write(f"// Auto-generated from {os.path.basename(raw_file_path)}\n")
        f.write(f"// {num_samples} samples, {len(data)} bytes\n")
        f.write(f"// 44100 Hz, 16-bit signed PCM, mono\n")
        f.write(f"// PROGMEM - stored in flash, not RAM\n")
        f.write(f"// AudioPlayMemory format\n\n")
        f.write(f"#ifndef {sample_name.upper()}_H\n")
        f.write(f"#define {sample_name.upper()}_H\n\n")
        f.write(f"#include <Arduino.h>\n\n")
        f.write(f"extern const unsigned int {sample_name}_data[{num_uint32}];\n\n")
        f.write(f"#endif // {sample_name.upper()}_H\n")

    # Also create a .cpp file with the actual data
    cpp_path = output_path.with_suffix('.cpp')
    with open(cpp_path, 'w') as f:
        f.write(f"// Auto-generated from {os.path.basename(raw_file_path)}\n")
        f.write(f"#include \"{output_path.name}\"\n\n")
        f.write(f"const unsigned int {sample_name}_data[{num_uint32}] PROGMEM = {{\n")

        # First element: format code (0x81 = 16-bit PCM 44100Hz) in upper 8 bits, sample count in lower 24 bits
        format_word = (0x81 << 24) | (num_samples & 0xFFFFFF)
        f.write(f"  0x{format_word:08X},  // Format: 0x81 (16-bit PCM 44100Hz), {num_samples} samples\n")

        # Pack int16 samples into uint32 (2 samples per uint32)
        for i in range(0, num_samples, 2):
            if i % 8 == 0:
                f.write("  ")

            # Pack two 16-bit samples into one 32-bit word (little-endian)
            sample1 = samples[i] & 0xFFFF
            sample2 = samples[i+1] & 0xFFFF if (i+1) < num_samples else 0
            packed = sample1 | (sample2 << 16)

            f.write(f"0x{packed:08X}")

            if i + 2 < num_samples:
                f.write(", ")

            if (i + 2) % 8 == 0 or i + 2 >= num_samples:
                f.write("\n")

        f.write("};\n")

def apply_fade_in(audio_data, fade_samples=44):
    """Apply fade-in to eliminate clicks (default 1ms @ 44100Hz)."""
    if len(audio_data) < fade_samples:
        fade_samples = len(audio_data)

    # Create fade-in envelope (linear)
    fade = np.linspace(0.0, 1.0, fade_samples)
    audio_data[:fade_samples] *= fade
    return audio_data

def apply_fade_out(audio_data, fade_samples=882):
    """Apply fade-out to eliminate end clicks (default 20ms @ 44100Hz)."""
    if len(audio_data) < fade_samples:
        fade_samples = len(audio_data)

    # Create fade-out envelope (linear)
    fade = np.linspace(1.0, 0.0, fade_samples)
    audio_data[-fade_samples:] *= fade
    return audio_data

def process_wav_to_raw(wav_path, output_dir, sample_name):
    """Convert WAV file to RAW and generate header."""
    print(f"\nProcessing: {wav_path}")

    # Read WAV file
    with wave.open(str(wav_path), 'rb') as wav:
        num_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        num_frames = wav.getnframes()

        print(f"  Original: {sample_rate}Hz, {num_channels}ch, {sample_width*8}-bit, {num_frames} frames")

        # Read audio data
        audio_bytes = wav.readframes(num_frames)

        # Convert to numpy array
        if sample_width == 2:
            audio_data = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0
        elif sample_width == 1:
            audio_data = (np.frombuffer(audio_bytes, dtype=np.uint8).astype(np.float32) - 128) / 128.0
        else:
            print(f"  ERROR: Unsupported sample width: {sample_width}")
            return None

    # Convert to mono
    audio_data = convert_to_mono(audio_data, num_channels)

    # Resample to 44100Hz
    audio_data = resample_audio(audio_data, sample_rate, 44100)

    # Trim silence
    audio_data = trim_silence(audio_data)

    # Debug: check first/last samples before fades
    print(f"  Before fades: first={audio_data[0]:.4f}, last={audio_data[-1]:.4f}")

    # Apply AGGRESSIVE 20ms fade-in to eliminate start clicks (882 samples @ 44100Hz)
    audio_data = apply_fade_in(audio_data, fade_samples=882)

    # Apply AGGRESSIVE 50ms fade-out to eliminate end clicks (2205 samples @ 44100Hz)
    audio_data = apply_fade_out(audio_data, fade_samples=2205)

    # Debug: check first/last samples after fades
    print(f"  After fades: first={audio_data[0]:.4f}, last={audio_data[-1]:.4f}")

    # Normalize
    audio_data = normalize_audio(audio_data)

    # Debug: check first/last samples after normalize
    print(f"  After normalize: first={audio_data[0]:.4f}, last={audio_data[-1]:.4f}")

    duration_ms = len(audio_data) / 44.1
    size_bytes = len(audio_data) * 2

    print(f"  Processed: 44100Hz, mono, 16-bit, {len(audio_data)} samples ({duration_ms:.0f}ms, {size_bytes} bytes)")

    # Save as RAW
    raw_path = output_dir / f"{sample_name}.raw"
    save_as_raw(audio_data, raw_path)

    # Generate C header and cpp files
    h_path = output_dir / f"{sample_name}.h"
    generate_c_header(sample_name, raw_path, h_path)
    cpp_path = output_dir / f"{sample_name}.cpp"

    print(f"  Generated: {raw_path.name}, {h_path.name}, {cpp_path.name}")

    return {
        'name': sample_name,
        'raw_file': raw_path.name,
        'h_file': h_path.name,
        'cpp_file': cpp_path.name,
        'size_bytes': size_bytes,
        'duration_ms': duration_ms,
        'samples': len(audio_data)
    }

def main():
    if len(sys.argv) < 3:
        print("Usage: python extract_drums.py input output_dir")
        print("")
        print("Input can be:")
        print("  - A .sf2 SoundFont file (extracts all drums automatically)")
        print("  - A directory with .wav files")
        print("")
        print("Output: RAW files + C header files for embedding in firmware")
        print("")
        print("Examples:")
        print("  python extract_drums.py 8mbgmsfx.sf2 output/")
        print("  python extract_drums.py my_wavs/ output/")
        sys.exit(1)

    input_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])

    if not input_path.exists():
        print(f"ERROR: Input not found: {input_path}")
        sys.exit(1)

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    wav_files = []

    # Check if input is an SF2 file
    if input_path.is_file() and input_path.suffix.lower() == '.sf2':
        print("="*70)
        print("SF2 SOUNDFONT EXTRACTION")
        print("="*70)

        # Extract drums from SF2
        extracted = extract_drums_from_sf2(input_path, output_dir)

        if not extracted:
            print("No drums extracted from SF2 file")
            sys.exit(1)

        # Get the WAV files we just created
        wav_files = [item['wav_path'] for item in extracted]
        print(f"\nExtracted {len(wav_files)} drum samples from SF2")

    # Check if input is a directory with WAV files
    elif input_path.is_dir():
        wav_files = list(input_path.glob("*.wav")) + list(input_path.glob("*.WAV"))

        if not wav_files:
            print(f"No WAV files found in {input_path}")
            sys.exit(1)

        print(f"Found {len(wav_files)} WAV files")

    else:
        print(f"ERROR: Input must be either:")
        print(f"  - A .sf2 file")
        print(f"  - A directory containing .wav files")
        sys.exit(1)

    print("\n" + "="*70)
    print("CONVERTING TO RAW + C HEADERS")
    print("="*70)

    results = []
    total_size = 0

    # Process each WAV file
    for wav_path in sorted(wav_files):
        sample_name = wav_path.stem.lower().replace(' ', '_').replace('-', '_')
        result = process_wav_to_raw(wav_path, output_dir, sample_name)
        if result:
            results.append(result)
            total_size += result['size_bytes']

    # Generate summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    print(f"{'Sample':<20} {'Size':<12} {'Duration':<12} {'Files':<30}")
    print("-"*70)

    for r in results:
        files = f"{r['h_file']}, {r['cpp_file']}"
        print(f"{r['name']:<20} {r['size_bytes']:>8} bytes  {r['duration_ms']:>6.0f} ms     {files}")

    print("-"*70)
    print(f"{'TOTAL':<20} {total_size:>8} bytes ({total_size/1024:.1f} KB)")
    print("="*70)

    # Generate MIDI mapping suggestion
    print("\n" + "="*70)
    print("SUGGESTED MIDI NOTE MAPPING")
    print("="*70)

    # Create a simple mapping based on common drum names
    mapping = {
        'kick': [35, 36],
        'bass_drum': [35, 36],
        'snare': [38, 40],
        'hh_closed': [42, 44],
        'hihat_closed': [42, 44],
        'hh_open': [46],
        'hihat_open': [46],
        'crash': [49, 57],
        'ride': [51, 59],
        'tom_low': [41, 43],
        'tom_mid': [45, 47],
        'tom_high': [48, 50],
        'rim': [37],
        'clap': [39],
    }

    for r in results:
        # Find matching mapping
        notes = []
        for pattern, note_list in mapping.items():
            if pattern in r['name']:
                notes = note_list
                break

        if notes:
            note_names = [f"{n} ({GM_DRUM_MAP.get(n, 'Unknown')})" for n in notes]
            print(f"{r['name']:<20} -> MIDI notes: {', '.join(note_names)}")

    print("="*70)
    print(f"\nGenerated {len(results)} samples in: {output_dir}")
    print("\nTo use in your project:")
    print("1. Copy the .h files to your src/ directory")
    print("2. Update drum_sampler_v2.cpp to reference these arrays")
    print("3. Update the MIDI note mapping")

if __name__ == "__main__":
    main()
