# README configuration demonstration

The GIF shows a continuous simulated configuration session in TextEdit. `menu-demo.cpp` runs the firmware's `DeviceMenu` with simulated connections and settings. It decodes the menu's HID reports into text. No device is connected or changed.

1. Build and export transcripts from the repository root:

   ```sh
   c++ -std=c++17 -Wall -Wextra -Werror -Isrc scripts/docs/menu-demo.cpp -o /tmp/hid-menu-demo
   /tmp/hid-menu-demo /tmp/hid-demo/transcripts
   ```

2. Open a new plain-text document in TextEdit. Use a monospace font. Do not use a document with existing work. Enable Accessibility and Screen Recording for Orca Computer Use.
3. Find the document with `orca computer list-windows --app com.apple.TextEdit --json`.
4. Capture the simulated steps. This clears the specified window once, then appends text in small chunks:

   ```sh
   python3 scripts/docs/capture-textedit.py /tmp/hid-demo --window-id WINDOW_ID
   ```

5. Encode the screenshots. Install Pillow in your Python environment first:

   ```sh
   python3 scripts/docs/encode-gifs.py /tmp/hid-demo/manifest.json docs/images
   ```

Check every frame before publication. Captions show physical actions; the menu does not echo selection keys. Menu output appends continuously at a simulated 32 ms per character. Name entry uses 100 ms per character. Captures group up to four characters per frame. These timings do not measure device latency.

`docs/images/header.svg` and `logo.svg` are editable vector artwork. The header includes the logo. No font or image download is required to display them.
