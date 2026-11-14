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
- **WPS detection flag**: `WifiTarget.wps` / `ApObserved.wps` / `CapturedFrame.wps`, wired
  through `dot11.hpp::parseTaggedParams` (WPS vendor IE `00:50:F2:04`) and
  `SecSession::ingest()`, passive and informational only (no new transmit capability —
  consistent with the existing authorization gate).
- **`SecMood`**: a small pure signal (`Calm` / `Watchful` / `Alarmed`) derived from recent
  alert severity (`deriveSecMood`), decoupled from the cross-platform `Mood` enum so
  hardware that has never heard of `secops.hpp` is unaffected.
- **Session record/replay wire format**: `encodeSecEvent`/`decodeSecEvent` (and the
  `...Events` / `replaySecEvents` batch helpers) — a pure, escaped, line-based text
  encoding for the `SecEvent` log. Lets a session be replayed deterministically through
  `reduce()` to reproduce the exact `SecState` it produced live. Wired into `zd-linux`
  (`--record`, `--replay`, `replay <file>`, and TUI `w`/`r` hotkeys) and `zd-gui`
  ("Load Session…" / "Save Session…").
- **Bounded collections in `SecState`**: `kMaxTargets`, `kMaxStations`, `kMaxAlerts`,
  `kMaxDeauthTrackers`, `kMaxBssidSsids`, and `kMaxSsidsPerBssid` cap all in-memory
  tables with LRU/FIFO eviction so multi-hour monitoring sessions have strictly bounded
  memory usage.

## Core follow-ups (wiring the above into a shell)

- [x] **Linux CLI**: `--record <file>` / `--replay <file>` flags and `replay <file>`
  subcommand on `zd-linux` (plus interactive `w`/`r` save/load keys in the TUI).
- [x] **Linux GUI**: "Load Session…" and "Save Session…" actions in `zd-gui` that replay or
  export recorded sessions without touching hardware.
- [x] Surface the station/probe table in the `zd-linux` TUI (`[p]` / `Tab` view toggle) and
  `zd-gui` (`Stations / Probes` tab), including a `WEAK!` / `WEAK / DEFAULT` flag
  (`hasWeakOrDefaultProbe`) for stations probing default/open-bait SSIDs.
- [x] Surface `WifiTarget.wps` (`WPS*`) in both the `zd-linux` and `zd-gui` network tables.
- [x] Surface `SecMood`/alert severity in the Linux GUI pet widget (amber/red bezel shift +
  `SEC: CALM/WATCHFUL/ALARMED` status badge) and `zd-linux` TUI header / `watch` stream.
- [ ] Detect WPS in active scan paths (`nmcli`/`iw` recon parsers) in addition to the
  monitor-mode 802.11 beacon/probe-response parser (`dot11.hpp`).

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
