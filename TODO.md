# Follow-up work

- Automatic fallback when the selected computer disconnects, with an explicit user preference.
- Investigate controller-completion latency spikes during simultaneous typing and mouse movement; distinguish them from end-to-end latency.
- Test three simultaneous hosts and Windows/Linux reconnection behavior.
- Explore vendor HID/HID++ passthrough so mouse configuration software can communicate with the original device. Advertising vendor IDs alone is insufficient.
- Investigate mouse hardware settings only after detecting supported vendor features; do not assume Motion Sync or configurable polling rate.
- Configuration backup/restore and additional LED feedback; per-host axis tuning and calibration are implemented.
- Extend the validated absolute mouse layout with horizontal pan and buttons 6–8 without regressing macOS dragging.
- Validate physical USB hub unplug/replug recovery; successful startup and software recovery are not sufficient proof.
- Additional host keyboard layouts and Unicode names in the device menu.
