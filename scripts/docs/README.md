# README demonstrations

The GIFs show simulated configuration in TextEdit. `menu-demo.cpp` runs the firmware's `DeviceMenu` with simulated connections and settings. It decodes the menu's HID reports into text. No device is connected or changed.

1. Build and export transcripts from the repository root:

   ```sh
   c++ -std=c++17 -Wall -Wextra -Werror -Isrc scripts/docs/menu-demo.cpp -o /tmp/hid-menu-demo
   /tmp/hid-menu-demo /tmp/hid-demo/transcripts
   ```

2. Open a new plain-text document in TextEdit. Use a monospace font. Do not use a document with existing work. Enable Accessibility and Screen Recording for Orca Computer Use.
3. Find the document with `orca computer list-windows --app com.apple.TextEdit --json`.
4. Capture the simulated steps. This replaces the contents of the specified window:

   ```sh
   python3 scripts/docs/capture-textedit.py /tmp/hid-demo --window-id WINDOW_ID
   ```

5. Encode the screenshots. Install Pillow in your Python environment first:

   ```sh
   python3 scripts/docs/encode-gifs.py /tmp/hid-demo/manifest.json docs/images
   ```

Check every frame before publication. Captions show physical actions; the menu does not echo selection keys. GIF timing is accelerated and does not measure device latency.

`docs/images/header.svg` and `logo.svg` are editable vector artwork. The header includes the logo. No font or image download is required to display them.
