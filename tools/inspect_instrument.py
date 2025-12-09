#!/usr/bin/env python3
"""Inspect SF2 instrument structure."""

from pathlib import Path
from sf2utils.sf2parse import Sf2File

sf2_path = Path("tools/Creative (emu10k1)8MBGMSFX.SF2")

print(f"Opening: {sf2_path}")
with open(sf2_path, 'rb') as sf2_file:
    sf2 = Sf2File(sf2_file)

# Find "Standard" drum preset
drum_preset = None
for preset in sf2.presets:
    try:
        name = preset.name if isinstance(preset.name, str) else preset.name.decode('utf-8', errors='ignore').strip('\x00')
        bank = preset.bank if hasattr(preset, 'bank') else 0
        if bank == 128 and 'standard' in name.lower():
            drum_preset = preset
            print(f"\nFound Standard drum preset: {name}")
            break
    except:
        pass

if not drum_preset:
    print("No standard drum preset found")
    exit(1)

# Get the instrument from first bag
bag = drum_preset.bags[0]
instrument = bag.instrument

print(f"\nInstrument: {instrument.name}")
print(f"Instrument has {len(instrument.bags)} bags")

# Look at a few bags to understand mapping
print("\n\nInspecting first 10 instrument bags:")
for i, inst_bag in enumerate(instrument.bags[:10]):
    key_range = inst_bag.key_range
    sample = inst_bag.sample

    if key_range:
        # key_range is a list [low, high]
        if isinstance(key_range, list) and len(key_range) == 2:
            low_key = key_range[0]
            high_key = key_range[1]
        else:
            low_key = key_range & 0xFF
            high_key = (key_range >> 8) & 0xFF

        print(f"\nBag {i}:")
        print(f"  Key range: {low_key}-{high_key}")

        if sample:
            sample_name = sample.name if isinstance(sample.name, str) else sample.name.decode('utf-8', errors='ignore').strip('\x00')
            print(f"  Sample: {sample_name}")
            print(f"  Sample rate: {sample.sample_rate}")
            print(f"  Sample has {len(sample.raw_sample_data)} bytes")
