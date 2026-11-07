// Pure domain state, Pwnagotchi moods/faces, phrase PRNG & reducer
// (ported from include/domain.hpp & include/pet_logic.hpp + playful hacker toy extensions).
(function (root) {
  'use strict';

  const fp = (root.ZD && root.ZD.fp) || (typeof require === 'function' ? require('./fp.js') : null);
  const wifi =
    (root.ZD && root.ZD.wifiAnalysis) ||
    (typeof require === 'function' ? require('./wifi_analysis.js') : null);
  const themes =
    (root.ZD && root.ZD.themes) ||
    (typeof require === 'function' ? require('./themes.js') : null);

  const Mood = Object.freeze({
    Awake: 'Awake',
    LookingLeft: 'LookingLeft',
    LookingRight: 'LookingRight',
    Happy: 'Happy',
    Excited: 'Excited',
    Cool: 'Cool',
    Lonely: 'Lonely',
    Bored: 'Bored',
    Sleepy: 'Sleepy',
    // Playful web-toy bonus moods
    Grateful: 'Grateful',
    Mischief: 'Mischief'
  });

  const ScreenPage = Object.freeze({
    PetFace: 'PetFace',
    NearbyRadar: 'NearbyRadar',
    ChannelSpectrum: 'ChannelSpectrum'
  });

  const ThemeId = themes.ThemeId;

  const EyeGlyph = Object.freeze({
    RoundCenter: 'RoundCenter',
    RoundLeft: 'RoundLeft',
    RoundRight: 'RoundRight',
    HappyArc: 'HappyArc',
    WideSparkle: 'WideSparkle',
    Sunglasses: 'Sunglasses',
    SadDroop: 'SadDroop',
    HalfLid: 'HalfLid',
    SleepingLine: 'SleepingLine',
    CyberWink: 'CyberWink'
  });

  const MouthGlyph = Object.freeze({
    DoubleSmile: 'DoubleSmile',
    BigGrin: 'BigGrin',
    Smirk: 'Smirk',
    FlatLine: 'FlatLine',
    SadWave: 'SadWave'
  });

  function faceForMood(mood) {
    switch (mood) {
      case Mood.Awake:
        return {
          eyes: EyeGlyph.RoundCenter,
          mouth: MouthGlyph.DoubleSmile,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '(o_o)',
          moodLabel: 'AWAKE'
        };
      case Mood.LookingLeft:
        return {
          eyes: EyeGlyph.RoundLeft,
          mouth: MouthGlyph.DoubleSmile,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '( o_o)',
          moodLabel: 'SCAN'
        };
      case Mood.LookingRight:
        return {
          eyes: EyeGlyph.RoundRight,
          mouth: MouthGlyph.DoubleSmile,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '(o_o )',
          moodLabel: 'SCAN'
        };
      case Mood.Happy:
        return {
          eyes: EyeGlyph.HappyArc,
          mouth: MouthGlyph.DoubleSmile,
          showBlush: true,
          showTear: false,
          showZzz: false,
          asciiFace: '(^_^)',
          moodLabel: 'HAPPY'
        };
      case Mood.Excited:
        return {
          eyes: EyeGlyph.WideSparkle,
          mouth: MouthGlyph.BigGrin,
          showBlush: true,
          showTear: false,
          showZzz: false,
          asciiFace: '(*O*)',
          moodLabel: 'HYPED'
        };
      case Mood.Cool:
        return {
          eyes: EyeGlyph.Sunglasses,
          mouth: MouthGlyph.Smirk,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '[B_B]',
          moodLabel: 'COOL'
        };
      case Mood.Lonely:
        return {
          eyes: EyeGlyph.SadDroop,
          mouth: MouthGlyph.SadWave,
          showBlush: false,
          showTear: true,
          showZzz: false,
          asciiFace: '(;_;)',
          moodLabel: 'LONELY'
        };
      case Mood.Bored:
        return {
          eyes: EyeGlyph.HalfLid,
          mouth: MouthGlyph.FlatLine,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '(._.)',
          moodLabel: 'BORED'
        };
      case Mood.Sleepy:
        return {
          eyes: EyeGlyph.SleepingLine,
          mouth: MouthGlyph.FlatLine,
          showBlush: false,
          showTear: false,
          showZzz: true,
          asciiFace: '(-_-)',
          moodLabel: 'SLEEPY'
        };
      case Mood.Grateful:
        return {
          eyes: EyeGlyph.HappyArc,
          mouth: MouthGlyph.BigGrin,
          showBlush: true,
          showTear: false,
          showZzz: false,
          asciiFace: '(^o^)',
          moodLabel: 'LUV!'
        };
      case Mood.Mischief:
        return {
          eyes: EyeGlyph.CyberWink,
          mouth: MouthGlyph.Smirk,
          showBlush: true,
          showTear: false,
          showZzz: false,
          asciiFace: '(>_*)',
          moodLabel: 'PWN!'
        };
      default:
        return {
          eyes: EyeGlyph.RoundCenter,
          mouth: MouthGlyph.DoubleSmile,
          showBlush: false,
          showTear: false,
          showZzz: false,
          asciiFace: '(o_o)',
          moodLabel: 'AWAKE'
        };
    }
  }

  const PAGE_ORDER = Object.freeze([
    ScreenPage.PetFace,
    ScreenPage.NearbyRadar,
    ScreenPage.ChannelSpectrum
  ]);

  function cyclePage(current, step) {
    const n = PAGE_ORDER.length;
    const base = PAGE_ORDER.indexOf(current);
    const safeBase = base >= 0 ? base : 0;
    const delta = Math.trunc(Number(step) || 0);
    const idx = (((safeBase + (delta % n)) % n) + n) % n;
    return PAGE_ORDER[idx];
  }

  function nextPage(current) {
    return cyclePage(current, +1);
  }

  function prevPage(current) {
    return cyclePage(current, -1);
  }

  const cycleTheme = themes.cycleTheme;
  const nextTheme = themes.nextTheme;
  const prevTheme = themes.prevTheme;

  function deriveMoodFromScan(summary) {
    if (summary.totalAps === 0) {
      return Mood.Lonely;
    }
    if (summary.totalAps >= 10) {
      return Mood.Excited;
    }
    if (summary.strongestRssi >= -50) {
      return Mood.Cool;
    }
    if (summary.totalAps >= 3) {
      return Mood.Happy;
    }
    return Mood.Bored;
  }

  function selectPhrase(mood, scan, seed) {
    switch (mood) {
      case Mood.Awake: {
        const kPhrases = [
          'Antenna warm! Ready to explore.',
          'Listening to 2.4GHz airwaves...',
          'Press [A] for views, [B] to scan!',
          'ZeroDay awake and feeling curious.'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.LookingLeft:
      case Mood.LookingRight: {
        const kPhrases = [
          'Looking around the airwaves...',
          'Sweeping WiFi channels 1 to 13...',
          'Counting nearby WiFi beacons...',
          'Scanning... anyone broadcasting?'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.Happy: {
        const s1 = fp.nextSeed(seed);
        if (s1 % 3 === 0 && scan.busiestChannel > 0) {
          return {
            value:
              'Spotted ' + scan.totalAps + ' APs! Ch ' + scan.busiestChannel + ' is cozy.',
            nextSeed: s1
          };
        }
        const kPhrases = [
          'Nice neighborhood! Good signals.',
          'Found friendly routers nearby~',
          'Cruising through clean airwaves!'
        ];
        return fp.pickFrom(s1, kPhrases);
      }
      case Mood.Excited: {
        const s1 = fp.nextSeed(seed);
        if (s1 % 2 === 0 && scan.totalAps > 0) {
          return {
            value: 'Whoa! ' + scan.totalAps + ' WiFi networks at once!!',
            nextSeed: s1
          };
        }
        const kPhrases = [
          'Spectrum party! Beacons everywhere!',
          'So many signals! My antenna tingles!',
          'Crowded airwaves! Look at all of em!'
        ];
        return fp.pickFrom(s1, kPhrases);
      }
      case Mood.Cool: {
        const s1 = fp.nextSeed(seed);
        if (s1 % 2 === 0 && scan.totalAps > 0) {
          return {
            value: 'Crisp ' + scan.strongestRssi + 'dBm signal right here!',
            nextSeed: s1
          };
        }
        const kPhrases = [
          'Shades on. Full bars in range.',
          'Crystal clear beacon right next door.',
          'Strongest signal in town. Smooth.'
        ];
        return fp.pickFrom(s1, kPhrases);
      }
      case Mood.Lonely: {
        const kPhrases = [
          'Zero networks... is anyone out there?',
          'Radio silence. Are we underground?',
          'Not a single beacon in sight...'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.Bored: {
        const kPhrases = [
          'Only a couple faint signals here.',
          "Quiet airwaves... let's take a walk!",
          'Same quiet routers on the horizon.'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.Sleepy: {
        const kPhrases = [
          'Zzz... resting my pixels...',
          'Power nap... press M5 to wake me.',
          'Dreaming of 5-bar beacons... zZz'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.Grateful: {
        const kPhrases = [
          'Nom nom! Fresh 802.11 snack~',
          'Hehe, that tickles my antenna!',
          'Happy pixels! Ready to hack!',
          'Purring at 2.4 GHz~ thank u!'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      case Mood.Mischief: {
        const kPhrases = [
          'Toy EAPOL bagged! Pure style.',
          'Zero low-level crimes, 100% vibes!',
          'Simulated 4-way handshake: GOTCHA!',
          'Hack the planet! (in sandbox mode)'
        ];
        return fp.pickFrom(seed, kPhrases);
      }
      default:
        return { value: 'Ready.', nextSeed: fp.nextSeed(seed) };
    }
  }

  function deriveLevelInfo(xp) {
    const safeXp = Math.max(0, Math.trunc(xp || 0));
    const tiers = [
      { level: 1, minXp: 0, title: 'Packet Sprout' },
      { level: 2, minXp: 40, title: 'Beacon Pup' },
      { level: 3, minXp: 110, title: 'Channel Surfer' },
      { level: 4, minXp: 220, title: 'Frame Ninja' },
      { level: 5, minXp: 380, title: '802.11 Wizard' },
      { level: 6, minXp: 600, title: 'ZeroDay Legend' }
    ];
    let current = tiers[0];
    let nextTier = tiers[1];
    for (let i = 0; i < tiers.length; i++) {
      if (safeXp >= tiers[i].minXp) {
        current = tiers[i];
        nextTier = tiers[i + 1] || null;
      }
    }
    let progressPct = 100;
    let nextXp = current.minXp;
    if (nextTier) {
      const span = nextTier.minXp - current.minXp;
      progressPct = Math.min(100, Math.round(((safeXp - current.minXp) / span) * 100));
      nextXp = nextTier.minXp;
    }
    return {
      level: current.level,
      title: current.title,
      xp: safeXp,
      nextXp,
      progressPct
    };
  }

  function sanitizePetName(rawName) {
    const cleaned = String(rawName == null ? '' : rawName)
      .replace(/[^a-zA-Z0-9_-]/g, '')
      .slice(0, 12);
    return cleaned.length > 0 ? cleaned : 'zeroday';
  }

  function initialState(bootSeed) {
    const seed = bootSeed === undefined ? 0x5EED2026 : (bootSeed >>> 0);
    return Object.freeze({
      uptimeMs: 0,
      lastScanStartMs: 0,
      lastScanCompleteMs: 0,
      lastInteractionMs: 0,
      lastPhraseChangeMs: 0,
      rngSeed: seed === 0 ? 0x5EED2026 : seed,
      completedScans: 0,
      peakApCount: 0,
      hasScannedOnce: false,
      isScanning: false,
      manualScanRequested: false,
      scanAnimTick: 0,
      batteryPercent: 100,
      isCharging: false,
      mood: Mood.Awake,
      page: ScreenPage.PetFace,
      theme: ThemeId.PaperInk,
      themeToastUntilMs: 0,
      petName: 'zeroday',
      phrase: 'Hello world! Ready to scan.',
      latestScan: wifi.emptyScanSummary(),
      // Playful hacker toy counters
      toyHandshakes: 0,
      snacksFed: 0,
      peersMet: 0,
      pokesCount: 0,
      xp: 0,
      lastPeerName: ''
    });
  }

  function shouldTriggerScan(state, nowMs, autoIntervalMs) {
    const interval = autoIntervalMs === undefined ? 12000 : autoIntervalMs;
    if (state.isScanning) {
      return false;
    }
    if (state.manualScanRequested) {
      return true;
    }
    if (!state.hasScannedOnce) {
      return nowMs >= 1500;
    }
    const effectiveInterval = state.mood === Mood.Sleepy ? interval * 2 : interval;
    return nowMs - state.lastScanCompleteMs >= effectiveInterval;
  }

  function selectThemePhrase(theme, seed) {
    const name = themes.themeSpec(theme).name;
    const s1 = fp.nextSeed(seed);
    switch (s1 % 4) {
      case 0:
        return { value: 'New look: ' + name + '!', nextSeed: s1 };
      case 1:
        return { value: 'Ooh, ' + name + ' suits me.', nextSeed: s1 };
      case 2:
        return { value: 'Repainted in ' + name + '~', nextSeed: s1 };
      default:
        return { value: 'Fresh pixels! ' + name + ' mode.', nextSeed: s1 };
    }
  }

  function applyPage(state, targetPage, nowMs) {
    const now = nowMs >>> 0;
    const validPage = PAGE_ORDER.includes(targetPage) ? targetPage : state.page;
    const next = Object.assign({}, state, {
      uptimeMs: now,
      lastInteractionMs: now,
      page: validPage
    });
    if (!next.isScanning && next.mood === Mood.Sleepy) {
      next.mood = next.hasScannedOnce ? deriveMoodFromScan(next.latestScan) : Mood.Awake;
      const picked = selectPhrase(next.mood, next.latestScan, next.rngSeed);
      next.phrase = picked.value;
      next.rngSeed = picked.nextSeed;
      next.lastPhraseChangeMs = now;
    }
    return Object.freeze(next);
  }

  function applyTheme(state, requestedTheme, nowMs, announce) {
    const now = nowMs >>> 0;
    const resolved = themes.isValidTheme(requestedTheme)
      ? requestedTheme
      : themes.themeFromKey(String(requestedTheme || ''));
    const nextThemeId = resolved || state.theme;
    const next = Object.assign({}, state, {
      uptimeMs: now,
      lastInteractionMs: now,
      theme: nextThemeId
    });
    if (announce !== false) {
      let until = (now + themes.THEME_TOAST_MS) >>> 0;
      if (until === 0) until = 1;
      next.themeToastUntilMs = until;
      if (!next.isScanning) {
        const picked = selectThemePhrase(nextThemeId, next.rngSeed);
        next.phrase = picked.value;
        next.rngSeed = picked.nextSeed;
        next.lastPhraseChangeMs = now;
      }
    }
    return Object.freeze(next);
  }

  function reduce(state, event) {
    if (!event || !event.type) return state;

    switch (event.type) {
      case 'Tick': {
        const next = Object.assign({}, state, {
          uptimeMs: event.nowMs >>> 0,
          batteryPercent: fp.clampVal(Math.trunc(event.batteryPercent), 0, 100),
          isCharging: Boolean(event.isCharging)
        });

        if (next.isScanning) {
          const phase = Math.floor((next.uptimeMs - next.lastScanStartMs) / 450) % 2;
          next.scanAnimTick = phase;
          next.mood = phase === 0 ? Mood.LookingLeft : Mood.LookingRight;
          return Object.freeze(next);
        }

        const quietArea = next.latestScan.totalAps <= 2;
        const idleLong =
          next.uptimeMs - next.lastInteractionMs >= 60000 && next.uptimeMs >= 60000;

        if (quietArea && idleLong && next.mood !== Mood.Sleepy) {
          next.mood = Mood.Sleepy;
          const picked = selectPhrase(Mood.Sleepy, next.latestScan, next.rngSeed);
          next.phrase = picked.value;
          next.rngSeed = picked.nextSeed;
          next.lastPhraseChangeMs = next.uptimeMs;
        } else if (next.uptimeMs - next.lastPhraseChangeMs >= 9000 && next.hasScannedOnce) {
          // Settle back to natural scan-derived mood after temporary toy excitement
          if (next.mood === Mood.Grateful || next.mood === Mood.Mischief) {
            next.mood = deriveMoodFromScan(next.latestScan);
          }
          const picked = selectPhrase(next.mood, next.latestScan, next.rngSeed);
          next.phrase = picked.value;
          next.rngSeed = picked.nextSeed;
          next.lastPhraseChangeMs = next.uptimeMs;
        }
        return Object.freeze(next);
      }

      case 'ButtonAPressed': {
        return applyPage(state, nextPage(state.page), event.nowMs);
      }

      case 'CyclePage': {
        return applyPage(
          state,
          cyclePage(state.page, event.step === undefined ? 1 : event.step),
          event.nowMs
        );
      }

      case 'ButtonBPressed': {
        const next = Object.assign({}, state, {
          uptimeMs: event.nowMs >>> 0,
          lastInteractionMs: event.nowMs >>> 0
        });
        if (!next.isScanning) {
          next.manualScanRequested = true;
        }
        return Object.freeze(next);
      }

      case 'ButtonBLongPressed': {
        return applyTheme(state, nextTheme(state.theme), event.nowMs, true);
      }

      case 'CycleTheme': {
        return applyTheme(
          state,
          cycleTheme(state.theme, event.step === undefined ? 1 : event.step),
          event.nowMs,
          event.announce !== false
        );
      }

      case 'ScanStarted': {
        const now = event.nowMs >>> 0;
        const picked = selectPhrase(Mood.LookingLeft, state.latestScan, state.rngSeed);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            isScanning: true,
            manualScanRequested: false,
            lastScanStartMs: now,
            scanAnimTick: 0,
            mood: Mood.LookingLeft,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now
          })
        );
      }

      case 'ScanCompleted': {
        const now = event.nowMs >>> 0;
        const summary = wifi.summarizeScan(event.rawNetworks || [], 12);
        const mood = deriveMoodFromScan(summary);
        const picked = selectPhrase(mood, summary, state.rngSeed);
        const xpGain = summary.totalAps > 0 ? 3 + Math.min(12, Math.floor(summary.totalAps / 2)) : 1;

        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            isScanning: false,
            manualScanRequested: false,
            hasScannedOnce: true,
            lastScanCompleteMs: now,
            completedScans: state.completedScans + 1,
            peakApCount: Math.max(state.peakApCount, summary.totalAps),
            latestScan: summary,
            mood,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now,
            xp: state.xp + xpGain
          })
        );
      }

      // --- Playful Web-Toy Events ---

      case 'PetPoked': {
        const now = event.nowMs >>> 0;
        const kPokes = [
          'Hehe! Careful with the antenna~',
          'Boop received! Pixels happy!',
          'Purring in 802.11b/g/n mode~',
          'System status: 100% good Lil Hacker!'
        ];
        const picked = fp.pickFrom(state.rngSeed, kPokes);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            lastInteractionMs: now,
            mood: state.isScanning ? state.mood : Mood.Grateful,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now,
            pokesCount: state.pokesCount + 1,
            xp: state.xp + 5
          })
        );
      }

      case 'SnackFed': {
        const now = event.nowMs >>> 0;
        const kSnacks = [
          'Nom nom! Crispy beacon bytes!',
          'Yum! Freshly baked 0xDEADBEEF!',
          'Crunching tasty packet cookies~',
          'Battery of my soul: recharged!'
        ];
        const picked = fp.pickFrom(state.rngSeed, kSnacks);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            lastInteractionMs: now,
            page: ScreenPage.PetFace,
            mood: state.isScanning ? state.mood : Mood.Grateful,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now,
            snacksFed: state.snacksFed + 1,
            xp: state.xp + 10
          })
        );
      }

      case 'ToyHandshake': {
        const now = event.nowMs >>> 0;
        const target =
          event.targetSsid ||
          (state.latestScan.strongestSsid ? state.latestScan.strongestSsid : 'TestLab_AP');
        const s1 = fp.nextSeed(state.rngSeed);
        const count = state.toyHandshakes + 1;
        const phrases = [
          'Toy pwn #' + count + ' on ' + target + '!',
          'Bagged virtual EAPOL from ' + target + '!',
          'Simulated handshake #' + count + ' -> /dev/fun!'
        ];
        const picked = fp.pickFrom(s1, phrases);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            lastInteractionMs: now,
            page: ScreenPage.PetFace,
            mood: state.isScanning ? state.mood : Mood.Mischief,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now,
            toyHandshakes: count,
            xp: state.xp + 25
          })
        );
      }

      case 'PeerSpotted': {
        const now = event.nowMs >>> 0;
        const peer = sanitizePetName(event.peerName || 'unit-404');
        const rssi = Number.isFinite(event.peerRssi) ? event.peerRssi : -44;
        const s1 = fp.nextSeed(state.rngSeed);
        const phrases = [
          'OMG! Peer [' + peer + '] (' + rssi + 'dB) waved hi!',
          'New fren! [' + peer + '] is chirping nearby!',
          'Synced memes with peer [' + peer + ']!'
        ];
        const picked = fp.pickFrom(s1, phrases);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            lastInteractionMs: now,
            page: ScreenPage.PetFace,
            mood: state.isScanning ? state.mood : Mood.Excited,
            phrase: picked.value,
            rngSeed: picked.nextSeed,
            lastPhraseChangeMs: now,
            peersMet: state.peersMet + 1,
            lastPeerName: peer,
            xp: state.xp + 20
          })
        );
      }

      case 'PetRenamed': {
        const now = event.nowMs >>> 0;
        const cleanName = sanitizePetName(event.petName);
        return Object.freeze(
          Object.assign({}, state, {
            uptimeMs: now,
            lastInteractionMs: now,
            petName: cleanName,
            phrase: 'Identity updated! Call me ' + cleanName + '.',
            lastPhraseChangeMs: now
          })
        );
      }

      case 'SetPage': {
        return applyPage(state, event.page, event.nowMs);
      }

      case 'SetTheme': {
        return applyTheme(state, event.theme, event.nowMs, event.announce !== false);
      }

      default:
        return state;
    }
  }

  const petLogic = Object.freeze({
    Mood,
    ScreenPage,
    ThemeId,
    EyeGlyph,
    MouthGlyph,
    faceForMood,
    cyclePage,
    nextPage,
    prevPage,
    cycleTheme,
    nextTheme,
    prevTheme,
    selectThemePhrase,
    deriveMoodFromScan,
    selectPhrase,
    deriveLevelInfo,
    sanitizePetName,
    initialState,
    shouldTriggerScan,
    reduce
  });

  root.ZD = root.ZD || {};
  root.ZD.petLogic = petLogic;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = petLogic;
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
