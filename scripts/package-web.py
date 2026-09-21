#!/usr/bin/env python3
"""Package the BLE-input build as a static installer, retaining NVS on updates."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import hashlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=Path('.pio/build/esp32s3_ble_input'))
parser.add_argument('--boot-app', type=Path, default=Path.home() / '.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin')
parser.add_argument('--output', type=Path, default=Path('artifacts/web-setup'))
parser.add_argument('--version', required=True)
args = parser.parse_args()
# Fail before copying anything if a build is missing or its layout differs.
parts = [('bootloader.bin', 0, args.build / 'bootloader.bin'),
         ('partitions.bin', 0x8000, args.build / 'partitions.bin'),
         ('boot_app0.bin', 0xe000, args.boot_app),
         ('firmware.bin', 0x10000, args.build / 'firmware.bin')]
limits = [0x8000, 0x9000, 0x10000, 0x310000]
for (_, offset, source), limit in zip(parts, limits):
    if not source.is_file() or not 0 < source.stat().st_size <= limit-offset:
        raise SystemExit(f'Missing/oversized image: {source}')
entries = {}
raw = (args.build / 'partitions.bin').read_bytes()
for pos in range(0, len(raw)-31, 32):
    magic, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', raw[pos:pos+32])
    if magic != 0x50aa:
        break
    entries[label.rstrip(b'\0').decode()] = (offset, size)
if entries.get('nvs') != (0x9000, 0x5000) or entries.get('otadata') != (0xe000, 0x2000) or entries.get('app0') != (0x10000, 0x300000):
    raise SystemExit('Unexpected partition layout; refusing to package')
args.output.mkdir(parents=True, exist_ok=True)
for name in ('index.html', 'style.css', 'app.mjs', 'serial.mjs', 'monitor-layout.mjs'):
    shutil.copy2(Path('web') / name, args.output / name)
firmware = args.output / 'firmware'; firmware.mkdir(exist_ok=True)
# Retain actual packaged binaries for downgrade selection. Paths use content
# hashes, never user-provided version strings; each manifest has local parts.
catalog_path = firmware / 'versions.json'
catalog = json.loads(catalog_path.read_text()) if catalog_path.exists() else {'versions': []}
def archive(manifest, directory, build_id=None):
    image = directory / 'firmware.bin'
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    key = hashlib.sha256((manifest['version'] + digest).encode()).hexdigest()[:24]
    folder = firmware / 'versions' / key; folder.mkdir(parents=True, exist_ok=True)
    for part in manifest['builds'][0]['parts']:
        name = part['path']
        if name not in ('bootloader.bin','partitions.bin','boot_app0.bin','firmware.bin'):
            raise SystemExit('Unexpected archived firmware part')
        shutil.copy2(directory / name, folder / name)
    (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    path = f'firmware/versions/{key}/manifest.json'
    existing = next((entry for entry in catalog['versions'] if entry['manifest'] == path), None)
    if existing is None:
        catalog['versions'].append({'version': manifest['version'], 'manifest': path, 'build_id': build_id or digest})
    elif build_id:
        existing['build_id'] = build_id
    return path
previous = firmware / 'manifest.json'
if previous.exists():
    archive(json.loads(previous.read_text()), firmware)
for name, _, source in parts:
    shutil.copy2(source, firmware / name)
manifest = {'name': 'HID Switcher BLE input (experimental)', 'version': args.version,
            'new_install_prompt_erase': True,
            'builds': [{'chipFamily': 'ESP32-S3', 'parts': [{'path': name, 'offset': offset} for name, offset, _ in parts]}]}
(firmware / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
elf = args.build / 'firmware.elf'
build_id = hashlib.sha256(elf.read_bytes()).hexdigest() if elf.exists() else None
catalog['current'] = archive(manifest, firmware, build_id)
catalog['versions'].sort(key=lambda entry: entry['manifest'] != catalog['current'])
catalog_path.write_text(json.dumps(catalog, indent=2) + '\n')
(args.output / '.nojekyll').touch()
print(args.output.resolve())
