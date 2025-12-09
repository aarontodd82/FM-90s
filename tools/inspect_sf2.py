#!/usr/bin/env python3
"""Quick script to inspect SF2 file structure."""

import sys
from pathlib import Path
from sf2utils.sf2parse import Sf2File

sf2_path = Path("tools/Creative (emu10k1)8MBGMSFX.SF2")

print(f"Opening: {sf2_path}")
with open(sf2_path, 'rb') as sf2_file:
    sf2 = Sf2File(sf2_file)

print(f"\nFound {len(sf2.presets)} presets")
print("\nInspecting first preset:")
preset = sf2.presets[0]

print(f"\nPreset attributes:")
for attr in dir(preset):
    if not attr.startswith('_'):
        try:
            value = getattr(preset, attr)
            print(f"  {attr}: {value}")
        except Exception as e:
            print(f"  {attr}: <error: {e}>")

print("\n\nLooking for drum presets:")
for i, preset in enumerate(sf2.presets[:20]):  # First 20 presets
    name = getattr(preset, 'name', 'unknown')
    # Try different possible attribute names for bank/preset number
    bank = None
    preset_num = None

    for attr_name in ['bank', 'bank_num', 'wBank', 'preset_bank']:
        if hasattr(preset, attr_name):
            bank = getattr(preset, attr_name)
            print(f"Found bank attribute: {attr_name} = {bank}")
            break

    for attr_name in ['preset', 'preset_num', 'wPreset', 'preset_number']:
        if hasattr(preset, attr_name):
            preset_num = getattr(preset, attr_name)
            break

    print(f"Preset {i}: name='{name}', bank={bank}, preset={preset_num}")
