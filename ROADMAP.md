# Roadmap

Ideas for pwnagotchi-zeroday, organized by where the work lands. Nothing here is committed
to a timeline — it's a backlog to pull from. Items under **Done** were implemented in the
shared core (`core/include/zd/`) and are available to every platform; everything else is
not yet implemented.

---

## Done (shared core)

- **Station / probe-request tracking** (`secops.hpp`): a `Station` record (MAC, probed
  SSIDs, last-associated BSSID, activity counters) fed by a new `ProbeRequestObserved`
  event, plus the existing `ClientObserved` association event. Surfaces phones/laptops
  leaking their preferred-network list via probe requests, independent of whether they're
  associated to anything nearby. Capped (`kMaxStations`, `kMaxProbedSsidsPerStation`) like
  the existing alert ring buffer. Wired through `secops_hal.hpp::SecSession::ingest()`,
  which already had the probed SSID decoded in `CapturedFrame` but was discarding it.
- **WPS detection flag**: `WifiTarget.wps` / `ApObserved.wps`, passive and informational
  only (no new transmit capability — consistent with the existing authorization gate).
- **`SecMood`**: a small pure signal (`Calm` / `Watchful` / `Alarmed`) derived from recent
  alert severity (`deriveSecMood`), decoupled from the cross-platform `Mood` enum so
  hardware that has never heard of `secops.hpp` is unaffected.
- **Session record/replay wire format**: `encodeSecEvent`/`decodeSecEvent` (and the
  `...Events` / `replaySecEvents` batch helpers) — a pure, escaped, line-based text
  encoding for the `SecEvent` log. Lets a session be replayed deterministically through
  `reduce()` to reproduce the exact `SecState` it produced live.

## Core follow-ups (wiring the above into a shell)

- [ ] **Linux CLI**: `--record <file>` / `--replay <file>` flags on `zd-linux watch` using
  `encodeSecEvents`/`decodeSecEvents` for file I/O (the pure half is done; this is just
  `fopen`/`fwrite` in `platforms/linux/app/controller.hpp`).
- [ ] **Linux GUI**: a "Load session" action in `zd-gui` that replays a recorded file
  instead of live capture, for demos/reports without touching hardware.
- [ ] Surface the station/probe table in the `zd-linux` TUI (new view) and `zd-gui` (new
  tab), including a flag for stations whose probed SSIDs match a known-weak/default
  pattern.
- [ ] Surface `WifiTarget.wps` in the network table (`*` style indicator next to the
  existing encryption marker).
- [ ] Surface `SecMood`/alert severity in the Linux GUI pet widget (color/face shift) and
  TUI status line, so `watch` is informative as ambient background status, not just a log.
- [ ] Actually detect WPS: `platforms/linux/backend/dot11.hpp::parseTaggedParams` doesn't
  check the WPS vendor IE (OUI `00-50-F2-04`) yet — the core field exists, nothing sets it
  from live frames yet. Same gap applies to any `nmcli`/`iw` recon parser path.

## Linux desktop (CLI/TUI + Qt6 GUI)

- [ ] **Assessment report export** (HTML/CSV): networks seen, alerts raised,
  handshakes/PMKIDs captured, generated from a session — a real deliverable for an
  authorized engagement, not just a live view.
- [ ] **GPS/wardriving mode**: tag `WifiTarget` with lat/lon from `gpsd`, pure KML/CSV
  exporter for mapping captured networks.
- [ ] **Multi-channel scheduling**: `channelDistribution`/`Band` classification already
  exist in `secops.hpp`; add a channel-hop policy so `watch` covers more of 2.4+5GHz
  instead of sitting on one fixed channel.
- [ ] **Handshake/PMKID export button** in the GUI to hand `.pcap`/`.22000` files to
  aircrack/hashcat — keeps cracking out of scope, just closes the capture→crack handoff.
- [ ] **Desktop notifications** (libnotify) for `Critical` alerts so `watch` is useful
  running in the background, not just foregrounded in a terminal/window.
- [ ] Config profiles ("lab" vs "field") presetting interface/channel/allow-list.

## M5Stick / ESP32

- [ ] **BLE scanning** alongside WiFi — the hardware already has the radio; a
  `BleObserved` event + a BLE radar view would be a natural sibling to the WiFi radar.
- [ ] **Known-network memory in NVS**: remember trusted BSSIDs across reboot so the pet is
  calm at home and alert near unknown APs.
- [ ] **Buzzer/haptic alerts** tied to mood/severity — `platforms/web/js/buzzer_sfx.js`
  already has the pattern for the browser; port it to the M5 piezo.

## Native desktop simulator (`zd_sim`)

- [ ] Scripted event-stream fuzzing against the pure reducers (secops + pet_logic) as a
  cheap regression harness — mostly a dev tool, not user-facing.
- [ ] Animated frame-sequence export (vs. the current single `.ppm`) for docs/README GIFs.

## Web port

- [ ] PWA packaging (manifest + service worker) for offline installability — it's already
  a pure client-side canvas toy, so this is nearly free.
- [ ] Shareable permalink encoding theme/view/demo-scan in the URL hash.

## Cross-cutting (not yet scoped to a specific platform)

- [ ] Pet mood (`Mood`/`faceForMood` in `pet_logic.hpp`) visibly reacting to `SecMood` —
  deliberately *not* done as part of the `SecMood` addition above, since it would mean
  adding a new `Mood` enumerator that ripples through every platform's face renderer
  (M5Stick, native, web JS mirror) and their parity tests. Worth doing once a single
  target is picked to prototype it (Linux GUI is the obvious first stop) rather than
  touching every platform at once.
