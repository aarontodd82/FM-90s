#!/usr/bin/env python3
"""Inspect SF2 bag/generator structure."""

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

print(f"\nPreset has {len(drum_preset.bags)} bags")
print("\nInspecting first bag:")
bag = drum_preset.bags[0]

print(f"\nBag type: {type(bag)}")
print(f"Bag attributes:")
for attr in dir(bag):
    if not attr.startswith('_'):
        try:
            value = getattr(bag, attr)
            print(f"  {attr}: {value} (type: {type(value).__name__})")
        except Exception as e:
            print(f"  {attr}: <error: {e}>")

# Try to get generators
print("\n\nTrying different ways to access generators:")

# Method 1: Direct attribute
if hasattr(bag, 'generators'):
    print("  bag.generators exists")
else:
    print("  bag.generators DOES NOT exist")

# Method 2: gens
if hasattr(bag, 'gens'):
    print("  bag.gens exists:")
    try:
        gens = list(bag.gens)
        print(f"    Found {len(gens)} generators")
        if gens:
            print(f"    First generator: {gens[0]}")
            print(f"    Generator attributes: {dir(gens[0])}")
    except Exception as e:
        print(f"    Error: {e}")

# Method 3: Check hydra_header
if hasattr(drum_preset, 'hydra_header'):
    print("\n  preset.hydra_header exists:")
    print(f"    Keys: {drum_preset.hydra_header.keys()}")
