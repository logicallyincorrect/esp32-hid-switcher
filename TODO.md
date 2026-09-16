# Follow-up work

- Automatic fallback when the selected computer disconnects, with an explicit user preference.
- Forget/replace a paired computer from the device menu.
- Investigate controller-completion latency spikes during simultaneous typing and mouse movement; distinguish them from end-to-end latency.
- Test three simultaneous hosts and Windows/Linux reconnection behavior.
- Explore vendor HID/HID++ passthrough so mouse configuration software can communicate with the original device. Advertising vendor IDs alone is insufficient.
- Investigate mouse hardware settings only after detecting supported vendor features; do not assume Motion Sync or configurable polling rate.
- Per-host mouse settings, configuration backup/restore, and additional LED feedback.
- Additional host keyboard layouts and Unicode names in the device menu.
