// Simulated 2.4 GHz RF airspace presets, roaming signal drift, custom fake beacon
// crafter, and friendly Pwnagotchi peers for the browser hacker toy.
(function (root) {
  'use strict';

  const fp = (root.ZD && root.ZD.fp) || (typeof require === 'function' ? require('./fp.js') : null);

  const PRESETS = Object.freeze({
    cafe: {
      id: 'cafe',
      label: 'Cyber Cafe',
      subtitle: 'Cozy urban signals (5-8 APs)',
      baseNetworks: [
        { ssid: 'c0ffee_house_2G', rssi: -56, channel: 6, secure: false },
        { ssid: 'CyberDeck_Tether', rssi: -61, channel: 1, secure: true },
        { ssid: 'MeshNode_West', rssi: -67, channel: 6, secure: true },
        { ssid: 'MeshNode_West', rssi: -76, channel: 11, secure: true },
        { ssid: 'TacoTruck_POS', rssi: -72, channel: 11, secure: true },
        { ssid: '', rssi: -80, channel: 3, secure: true },
        { ssid: 'Bookstore_Guest', rssi: -83, channel: 9, secure: false }
      ]
    },
    con: {
      id: 'con',
      label: 'SecCon Floor',
      subtitle: 'Packed hacker spectrum (14+ APs)',
      baseNetworks: [
        { ssid: 'SecCon-Open', rssi: -46, channel: 6, secure: false },
        { ssid: 'Wall_Of_Sheep', rssi: -51, channel: 1, secure: false },
        { ssid: 'BadgeLife_Mesh', rssi: -54, channel: 11, secure: true },
        { ssid: 'Gibson_Mainframe', rssi: -58, channel: 6, secure: true },
        { ssid: 'Packet_Village', rssi: -60, channel: 1, secure: true },
        { ssid: 'Rogue_Twin_AP_7', rssi: -63, channel: 6, secure: false },
        { ssid: 'Drop_Tables_LAN', rssi: -65, channel: 11, secure: true },
        { ssid: 'ZeroCool_Uplink', rssi: -68, channel: 3, secure: true },
        { ssid: 'AcidBurn_Deck', rssi: -70, channel: 8, secure: true },
        { ssid: 'ESP32_Swarm', rssi: -72, channel: 6, secure: true },
        { ssid: 'ESP32_Swarm', rssi: -79, channel: 1, secure: true },
        { ssid: 'Free_0dayz_Here', rssi: -74, channel: 13, secure: false },
        { ssid: '', rssi: -77, channel: 11, secure: true },
        { ssid: 'r00t_lounge', rssi: -81, channel: 4, secure: true },
        { ssid: 'HardwareHacking', rssi: -85, channel: 9, secure: true }
      ]
    },
    lab: {
      id: 'lab',
      label: 'ZeroDay Lab',
      subtitle: 'Booming bench signal (-41 dBm)',
      baseNetworks: [
        { ssid: 'ZeroDay_Bench_2G', rssi: -41, channel: 6, secure: true },
        { ssid: 'Pocket_TestRig', rssi: -53, channel: 1, secure: true },
        { ssid: 'LogicAnalyzer_AP', rssi: -64, channel: 11, secure: true },
        { ssid: 'IoT_Sandbox_Open', rssi: -73, channel: 6, secure: false }
      ]
    },
    quiet: {
      id: 'quiet',
      label: 'Night Suburb',
      subtitle: 'Faint horizon routers (1-2 APs)',
      baseNetworks: [
        { ssid: 'Distant_Router', rssi: -83, channel: 11, secure: true },
        { ssid: '', rssi: -89, channel: 1, secure: true }
      ]
    },
    bunker: {
      id: 'bunker',
      label: 'Faraday Cage',
      subtitle: 'Total radio silence (0 APs)',
      baseNetworks: []
    },
    roam: {
      id: 'roam',
      label: 'Auto-Wardrive',
      subtitle: 'Drifts across neighborhoods',
      baseNetworks: []
    }
  });

  const FUNNY_HACKER_SSIDS = Object.freeze([
    'PrettyFlyForAWiFi',
    'FBI_Van_#404',
    'Shadow_Clan_LAN',
    'sudo_make_sandwich',
    '404_Net_Not_Found',
    'Cyber_Defense_2G',
    'It_Hurts_When_IP',
    'TellMyWiFiLoveHer',
    'LAN_Solo',
    'Router_McRouterface',
    'Pocket_SubGHz_Friend',
    'Pwnagotchi_SnackBar',
    'rm_-rf_slash_wifi',
    'chmod_777_guest',
    'Alice_And_Bob_Key',
    'Schrodingers_AP'
  ]);

  const PEER_ROSTER = Object.freeze([
    'cyber-neko',
    'unit-404',
    'bit-gremlin',
    'm5-ghost',
    'sudo-pup',
    'pkt-fox',
    'nyan-pwn',
    'null-byte'
  ]);

  function resolveRoamSubPreset(nowMs) {
    const order = ['cafe', 'lab', 'con', 'quiet'];
    const slot = Math.floor(Math.max(0, nowMs) / 24000) % order.length;
    return order[slot];
  }

  // Purely generates a realistic scan snapshot from a preset + custom injected APs + PRNG seed.
  function generateAirwaveSnapshot(presetId, customAps, seed, nowMs) {
    let s = fp.nextSeed(seed);
    const effectiveId = presetId === 'roam' ? resolveRoamSubPreset(nowMs || 0) : presetId;
    const preset = PRESETS[effectiveId] || PRESETS.cafe;

    const generated = [];
    for (let i = 0; i < preset.baseNetworks.length; i++) {
      const base = preset.baseNetworks[i];
      s = fp.nextSeed(s);
      // Small occasional dropout on distant APs for organic realism
      if (base.rssi <= -82 && s % 5 === 0) {
        continue;
      }
      s = fp.nextSeed(s);
      const jitter = (s % 9) - 4; // -4..+4 dBm
      generated.push({
        ssid: base.ssid,
        rssi: fp.clampVal(base.rssi + jitter, -96, -28),
        channel: base.channel,
        secure: base.secure
      });
    }

    // Occasionally sprinkle a transient passerby hotspot in cafe/con modes
    if (effectiveId === 'cafe' || effectiveId === 'con') {
      s = fp.nextSeed(s);
      if (s % 2 === 0) {
        const pick = fp.pickFrom(s, FUNNY_HACKER_SSIDS);
        s = pick.nextSeed;
        const ch = ((s >>> 3) % 11) + 1;
        const rssi = -58 - (s % 28);
        generated.push({
          ssid: pick.value,
          rssi,
          channel: ch,
          secure: (s & 1) === 1
        });
      }
    }

    // Always include user-crafted custom beacons (with tiny 1-2 dBm breath)
    if (Array.isArray(customAps)) {
      for (let j = 0; j < customAps.length; j++) {
        const cap = customAps[j];
        if (cap && cap.enabled !== false) {
          s = fp.nextSeed(s);
          const microJitter = (s % 3) - 1;
          generated.push({
            ssid: cap.ssid,
            rssi: fp.clampVal(cap.rssi + microJitter, -98, -25),
            channel: cap.channel,
            secure: Boolean(cap.secure)
          });
        }
      }
    }

    return {
      networks: generated,
      effectivePresetId: effectiveId,
      nextSeed: s
    };
  }

  function randomHackerBeacon(seed) {
    const p1 = fp.pickFrom(seed, FUNNY_HACKER_SSIDS);
    const s2 = fp.nextSeed(p1.nextSeed);
    const channels = [1, 6, 11, 3, 9, 13, 6, 1];
    const channel = channels[s2 % channels.length];
    const s3 = fp.nextSeed(s2);
    const rssi = -42 - (s3 % 40);
    const secure = ((s3 >>> 4) & 1) === 1;
    return {
      beacon: {
        ssid: p1.value,
        channel,
        rssi,
        secure,
        enabled: true
      },
      nextSeed: s3
    };
  }

  function randomPeerEncounter(seed) {
    const p = fp.pickFrom(seed, PEER_ROSTER);
    const s2 = fp.nextSeed(p.nextSeed);
    const rssi = -38 - (s2 % 26);
    return {
      peerName: p.value,
      peerRssi: rssi,
      nextSeed: s2
    };
  }

  const airwavesSim = Object.freeze({
    PRESETS,
    FUNNY_HACKER_SSIDS,
    PEER_ROSTER,
    resolveRoamSubPreset,
    generateAirwaveSnapshot,
    randomHackerBeacon,
    randomPeerEncounter
  });

  root.ZD = root.ZD || {};
  root.ZD.airwavesSim = airwavesSim;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = airwavesSim;
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
