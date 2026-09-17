# Edge protocol v1

Service: `e963a000-8f4d-4a7b-9b65-62485c81a320`.

Sample characteristic: `e963a001-8f4d-4a7b-9b65-62485c81a320`, write with response on an encrypted, bonded connection.

Status characteristic: `e963a002-8f4d-4a7b-9b65-62485c81a320`, notifications sent only to the authenticated peer. Subscribe before sending samples. No pairing credentials or persistent settings are exposed.

Each message is 12 bytes. Integers use little-endian byte order. Reserved bytes must be zero.

| Offset | Sample: companion to firmware | Status: firmware to companion |
| --- | --- | --- |
| 0 | Version: 1 | Version: 1 |
| 1 | Edge: 0 none, 1 left, 2 right | Recipient slot: 0–2 |
| 2 | Bit 0 dragging, bit 1 enabled | Selected slot: 0–2 |
| 3 | Reserved | Requested entry edge: 0 none, 1 left, 2 right |
| 4–5 | Vertical position: 0–65535 over desktop height | Requested vertical position |
| 6–9 | Last received epoch | Current epoch |
| 10 | Reserved | Bit 0: pointer placement requested |
| 11 | Reserved | Reserved |

Firmware authenticates the connection identity; clients cannot claim another slot. Samples expire after 600 ms. The companion sends on edge/drag transitions and at least every 200 ms, with at most one write in flight. Firmware sends status on epoch changes and every 500 ms while subscribed.

An edge push switches immediately to the next or previous ready HID connection. Only the source needs fresh companion samples. Destination companion availability and pointer placement do not gate routing. After switching, the new epoch can carry a best-effort placement request for up to 750 ms. A subscribed destination companion attempts placement once per epoch. Its response is not an acknowledgment requirement; failure or silence does not undo the switch.

Menu entry, held-input inhibition, changes to ready hosts, disconnects, settings changes, and selection changes invalidate edge state. After invalidation, an interior sample is required before another edge push. A missing source companion or permission disables edge detection on that computer; manual shortcuts remain available.
