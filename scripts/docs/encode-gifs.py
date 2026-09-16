"""Encode the captured text viewport without app chrome or captions. Requires Pillow."""
import argparse
import json
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('manifest', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for kind, entries in json.loads(args.manifest.read_text()).items():
    if kind != 'name':
        continue
    frames, durations = [], []
    for entry in entries:
        image = Image.open(entry['file']).convert('RGB')
        # Captured at 2x: omit the 32pt title bar and scrollbars.
        image = image.crop((0, 64, image.width - 28, image.height - 30))
        image = image.resize((808, round(image.height * 808 / image.width)), Image.Resampling.LANCZOS)
        frames.append(image.quantize(colors=128))
        durations.append(entry['duration_ms'])
    durations[-1] = 4500
    path = args.output / 'configuration.gif'
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True, disposal=1)
    print(f'{path}: {len(frames)} frames, {sum(durations)/1000:.2f}s, {path.stat().st_size} bytes')
