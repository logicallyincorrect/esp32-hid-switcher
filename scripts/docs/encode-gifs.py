"""Encode captured TextEdit frames with action captions. Requires Pillow."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument('manifest', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--font', default='/System/Library/Fonts/Supplemental/Arial.ttf')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
font = ImageFont.truetype(args.font, 18)
small = ImageFont.truetype(args.font, 12)
for kind, entries in json.loads(args.manifest.read_text()).items():
    frames, durations = [], []
    for entry in entries:
        image = Image.open(entry['file']).convert('RGB')
        image = image.resize((808, round(image.height * 808 / image.width)), Image.Resampling.LANCZOS)
        frame = Image.new('RGB', (image.width, image.height + 76), '#10242b')
        frame.paste(image, (0, 0))
        draw = ImageDraw.Draw(frame)
        draw.text((20, image.height + 13), entry['caption'], font=font, fill='#79edbe')
        draw.text((20, image.height + 43), 'SIMULATED CONFIGURATION  /  TextEdit capture  /  Playback accelerated', font=small, fill='#b6cbce')
        frames.append(frame.quantize(colors=128))
        durations.append(entry['duration_ms'])
    durations[-1] = 4500
    path = args.output / ('rename-computer.gif' if kind == 'name' else 'record-mouse-shortcut.gif')
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True, disposal=2)
    print(f'{path}: {len(frames)} frames, {sum(durations)/1000:.2f}s, {path.stat().st_size} bytes')
