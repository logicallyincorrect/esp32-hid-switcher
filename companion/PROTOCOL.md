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
| 3 | Reserved | Unused; zero |
| 4–5 | Unused; older clients may send height | Unused; zero |
| 6–9 | Last received epoch | Current epoch |
| 10 | Reserved | Unused; zero |
| 11 | Reserved | Reserved |

Firmware authenticates the connection identity; clients cannot claim another slot. Samples expire after 600 ms. The companion sends on edge/drag transitions and at least every 200 ms, with at most one write in flight. Firmware sends status on epoch changes and every 500 ms while subscribed.

The first outward USB movement at the reported edge switches to the next or previous ready HID connection. Only the source needs fresh companion samples. There is no dwell timer, distance threshold, reposition request, or acknowledgment. The 800 ms cooldown and interior-sample rearming prevent repeated switching. The 12-byte v1 layout is retained; old clients can still report edges, but firmware never requests pointer placement. The companion ignores legacy placement fields.

Menu entry, held-input inhibition, changes to ready hosts, disconnects, settings changes, and selection changes invalidate edge state. After invalidation, an interior sample is required before another edge push. A missing source companion or permission disables edge detection on that computer; manual shortcuts remain available.
