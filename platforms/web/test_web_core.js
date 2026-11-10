// Automated Node.js unit tests for the pwnagotchi-zeroday Web Port Functional Core & Pixel Renderer.
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const fp = require('./js/fp.js');
const wifi = require('./js/wifi_analysis.js');
const themes = require('./js/themes.js');
const logic = require('./js/pet_logic.js');
const ui = require('./js/ui_model.js');
const sim = require('./js/airwaves_sim.js');
const gfxMod = require('./js/canvas_gfx.js');

function testFunctionalCombinators() {
  const nums = [5, 1, 4, 2, 3, 6];

  const result = fp.pipe(
    nums,
    fp.filtered((x) => x % 2 === 0),
    fp.mapped((x) => x * 10),
    fp.sortedBy((a, b) => a > b),
    fp.taken(2)
  );

  assert.deepStrictEqual(nums, [5, 1, 4, 2, 3, 6]);
  assert.deepStrictEqual(result, [60, 40]);

  const sum = fp.foldl(nums, 0, (acc, x) => acc + x);
  assert.strictEqual(sum, 21);

  // Exact deterministic PRNG parity
  const kItems = ['alpha', 'beta', 'gamma'];
  const p1 = fp.pickFrom(12345, kItems);
  const p2 = fp.pickFrom(12345, kItems);
  assert.strictEqual(p1.value, p2.value);
  assert.strictEqual(p1.nextSeed, p2.nextSeed);
  assert.ok(p1.nextSeed > 0 && p1.nextSeed <= 0xFFFFFFFF);
}

function testWifiAnalysisPipeline() {
  const raw = [
    { ssid: 'MeshNet', rssi: -72, channel: 6, secure: true },
    { ssid: 'MeshNet', rssi: -49, channel: 6, secure: true },
    { ssid: '', rssi: -81, channel: 11, secure: true },
    { ssid: 'CafeFree', rssi: -63, channel: 1, secure: false },
    { ssid: 'LabRouter\n', rssi: -55, channel: 6, secure: true }
  ];

  const summary = wifi.summarizeScan(raw, 6);
  assert.strictEqual(summary.totalAps, 5);
  assert.strictEqual(summary.secureAps, 4);
  assert.strictEqual(summary.openAps, 1);
  assert.strictEqual(summary.busiestChannel, 6);
  assert.strictEqual(summary.strongestRssi, -49);
  assert.strictEqual(summary.strongestSsid, 'MeshNet');

  assert.strictEqual(summary.topNetworks.length, 4);
  assert.strictEqual(summary.topNetworks[0].ssid, 'MeshNet');
  assert.strictEqual(summary.topNetworks[0].rssi, -49);
  assert.strictEqual(summary.topNetworks[1].ssid, 'LabRouter');
  assert.strictEqual(summary.topNetworks[2].ssid, 'CafeFree');
  assert.strictEqual(summary.topNetworks[3].ssid, '<hidden>');

  assert.strictEqual(wifi.rssiToBars(-48), 4);
  assert.strictEqual(wifi.rssiToBars(-62), 3);
  assert.strictEqual(wifi.rssiToBars(-75), 2);
  assert.strictEqual(wifi.rssiToBars(-85), 1);
  assert.strictEqual(wifi.rssiToBars(-94), 0);
}

function testStateReducerAndPlayfulEvents() {
  const s0 = logic.initialState(42);
  assert.strictEqual(logic.shouldTriggerScan(s0, 500), false);
  assert.strictEqual(logic.shouldTriggerScan(s0, 1600), true);

  const s1 = logic.reduce(s0, { type: 'ScanStarted', nowMs: 1600 });
  assert.strictEqual(s1.isScanning, true);
  assert.strictEqual(s1.mood, logic.Mood.LookingLeft);
  assert.strictEqual(s0.isScanning, false);

  const s2 = logic.reduce(s1, {
    type: 'Tick',
    nowMs: 2100,
    batteryPercent: 88,
    isCharging: false
  });
  assert.strictEqual(s2.mood, logic.Mood.LookingRight);
  assert.strictEqual(s2.batteryPercent, 88);

  const sLonely = logic.reduce(s2, {
    type: 'ScanCompleted',
    nowMs: 3200,
    rawNetworks: []
  });
  assert.strictEqual(sLonely.isScanning, false);
  assert.strictEqual(sLonely.mood, logic.Mood.Lonely);
  assert.strictEqual(sLonely.completedScans, 1);

  const sCool = logic.reduce(sLonely, {
    type: 'ScanCompleted',
    nowMs: 15500,
    rawNetworks: [
      { ssid: 'ZeroDayLab', rssi: -45, channel: 6, secure: true },
      { ssid: 'Guest', rssi: -75, channel: 1, secure: false }
    ]
  });
  assert.strictEqual(sCool.mood, logic.Mood.Cool);

  const crowd = [];
  for (let i = 0; i < 11; i++) {
    crowd.push({ ssid: 'AP_' + i, rssi: -65 - i, channel: (i % 11) + 1, secure: true });
  }
  const sExcited = logic.reduce(sCool, {
    type: 'ScanCompleted',
    nowMs: 28000,
    rawNetworks: crowd
  });
  assert.strictEqual(sExcited.mood, logic.Mood.Excited);
  assert.strictEqual(sExcited.peakApCount, 11);

  const sQuiet = logic.reduce(sExcited, {
    type: 'ScanCompleted',
    nowMs: 30000,
    rawNetworks: [{ ssid: 'FaintAP', rssi: -84, channel: 11, secure: true }]
  });
  assert.strictEqual(sQuiet.mood, logic.Mood.Bored);

  const sSleepy = logic.reduce(sQuiet, {
    type: 'Tick',
    nowMs: 95000,
    batteryPercent: 80,
    isCharging: true
  });
  assert.strictEqual(sSleepy.mood, logic.Mood.Sleepy);
  assert.strictEqual(sSleepy.isCharging, true);

  const sWoken = logic.reduce(sSleepy, { type: 'ButtonAPressed', nowMs: 96000 });
  assert.strictEqual(sWoken.mood, logic.Mood.Bored);
  assert.strictEqual(sWoken.page, logic.ScreenPage.NearbyRadar);

  const sThemed = logic.reduce(sWoken, { type: 'ButtonBLongPressed', nowMs: 96500 });
  assert.strictEqual(sThemed.theme, logic.ThemeId.DarkInk);

  const sManual = logic.reduce(sThemed, { type: 'ButtonBPressed', nowMs: 97000 });
  assert.strictEqual(sManual.manualScanRequested, true);
  assert.strictEqual(logic.shouldTriggerScan(sManual, 97010), true);

  // Playful hacker toy events
  const sSnack = logic.reduce(sManual, { type: 'SnackFed', nowMs: 98000 });
  assert.strictEqual(sSnack.mood, logic.Mood.Grateful);
  assert.strictEqual(sSnack.snacksFed, 1);

  const sPwn = logic.reduce(sSnack, {
    type: 'ToyHandshake',
    nowMs: 99000,
    targetSsid: 'Gibson_Mainframe'
  });
  assert.strictEqual(sPwn.mood, logic.Mood.Mischief);
  assert.strictEqual(sPwn.toyHandshakes, 1);

  const sPeer = logic.reduce(sPwn, {
    type: 'PeerSpotted',
    nowMs: 100000,
    peerName: 'cyber-neko',
    peerRssi: -42
  });
  assert.strictEqual(sPeer.mood, logic.Mood.Excited);
  assert.strictEqual(sPeer.peersMet, 1);
  assert.strictEqual(sPeer.lastPeerName, 'cyber-neko');

  const sNamed = logic.reduce(sPeer, {
    type: 'PetRenamed',
    nowMs: 101000,
    petName: 'n3k0_pwn!'
  });
  assert.strictEqual(sNamed.petName, 'n3k0_pwn');
}

function testResponsiveUiAndPixelRasterizer() {
  assert.strictEqual(gfxMod.FONT_5X7.length, 95);
  for (let i = 0; i < 95; i++) {
    assert.strictEqual(gfxMod.FONT_5X7[i].length, 5);
  }

  let state = logic.initialState(99);
  const snap = sim.generateAirwaveSnapshot('con', [{ ssid: 'Custom_0day', rssi: -40, channel: 6, secure: false }], 777, 5000);
  assert.ok(snap.networks.length >= 12);

  state = logic.reduce(state, {
    type: 'ScanCompleted',
    nowMs: 75000,
    rawNetworks: snap.networks
  });

  // 240x135 Plus2 frame
  const plusFrame = ui.buildUiFrame(state, { width: 240, height: 135 });
  assert.strictEqual(plusFrame.compact, false);
  assert.ok(plusFrame.headerLine.length <= 39);
  assert.ok(plusFrame.footerLine.length <= 39);

  // 160x80 Classic frame
  const compactFrame = ui.buildUiFrame(state, { width: 160, height: 80 });
  assert.strictEqual(compactFrame.compact, true);
  assert.ok(compactFrame.headerLine.length <= 26);
  assert.ok(compactFrame.footerLine.length <= 26);

  // Paint every mood and every page onto pixel buffer without error and verify foreground pixels exist
  const allMoods = Object.values(logic.Mood);
  for (const mood of allMoods) {
    const moodState = Object.assign({}, state, { mood });
    const frame = ui.buildUiFrame(moodState, { width: 240, height: 135 });
    const buf = gfxMod.createPixelBuffer(240, 135);
    gfxMod.paintUiFrame(buf, frame, { blink: false });

    let nonBgPixels = 0;
    for (let p = 0; p < buf.data.length; p += 4) {
      if (
        buf.data[p] !== frame.palette.bg.r ||
        buf.data[p + 1] !== frame.palette.bg.g ||
        buf.data[p + 2] !== frame.palette.bg.b
      ) {
        nonBgPixels++;
      }
    }
    assert.ok(nonBgPixels > 250, 'Expected visible foreground pixels for mood ' + mood);
  }

  for (const page of Object.values(logic.ScreenPage)) {
    const pageState = Object.assign({}, state, { page });
    const frame = ui.buildUiFrame(pageState, { width: 240, height: 135 });
    const buf = gfxMod.createPixelBuffer(240, 135);
    gfxMod.paintUiFrame(buf, frame);
    const ascii = ui.renderAsciiPreview(frame);
    assert.ok(ascii.includes('+------------------------------------------+'));
  }

  console.log('=== Web Port ASCII Preview (240x135) ===\n' + ui.renderAsciiPreview(plusFrame));
}

function testThemesMultiDeviceAndCppParity() {
  assert.strictEqual(themes.THEME_COUNT, 10);

  // Bidirectional cycling visits all 10 themes and wraps cleanly
  let cur = themes.ThemeId.PaperInk;
  const seen = new Set();
  for (let i = 0; i < themes.THEME_COUNT; i++) {
    seen.add(cur);
    cur = themes.nextTheme(cur);
  }
  assert.strictEqual(cur, themes.ThemeId.PaperInk);
  assert.strictEqual(seen.size, themes.THEME_COUNT);
  assert.strictEqual(themes.prevTheme(themes.ThemeId.PaperInk), themes.ThemeId.SunsetGrid);

  // Toast lifecycle & multi-device rendering across all 5 screen profiles
  let s = logic.initialState(77);
  s = logic.reduce(s, { type: 'SetTheme', nowMs: 5000, theme: 'operator' });
  assert.strictEqual(s.theme, themes.ThemeId.OperatorBlue);
  assert.ok(s.themeToastUntilMs === 5000 + themes.THEME_TOAST_MS);

  for (const prof of ui.SCREEN_PROFILES) {
    assert.strictEqual(ui.screenProfileFromKey(prof.key), prof);
    for (const spec of themes.THEME_CATALOG) {
      const st = logic.reduce(s, { type: 'SetTheme', nowMs: 6000, theme: spec.key });
      const frame = ui.buildUiFrame(st, prof.dims);
      assert.strictEqual(frame.toast.visible, true);
      assert.ok(frame.toast.x >= 0 && frame.toast.x + frame.toast.w <= prof.dims.width);
      assert.ok(frame.toast.y > frame.headerDividerY && frame.toast.y + frame.toast.h < frame.footerDividerY);

      const buf = gfxMod.createPixelBuffer(prof.dims.width, prof.dims.height);
      gfxMod.paintUiFrame(buf, frame);
    }
  }

  const expired = logic.reduce(s, {
    type: 'Tick',
    nowMs: 5000 + themes.THEME_TOAST_MS,
    batteryPercent: 95,
    isCharging: false
  });
  assert.strictEqual(ui.buildUiFrame(expired, { width: 240, height: 135 }).toast.visible, false);

  // Trademark & brand cleanliness check on theme metadata
  const bannedTerms = [
    'kali',
    'offsec',
    'backtrack',
    'cyberpunk',
    'arasaka',
    'cdpr',
    'night city',
    'ubuntu',
    'flipper',
    'hak5',
    'nintendo',
    'apple',
    'microsoft',
    'matrix'
  ];
  for (const spec of themes.THEME_CATALOG) {
    const combined = (spec.id + ' ' + spec.key + ' ' + spec.name + ' ' + spec.blurb).toLowerCase();
    for (const term of bannedTerms) {
      assert.strictEqual(
        combined.includes(term),
        false,
        'Theme "' + spec.key + '" must not contain restricted brand term "' + term + '"'
      );
    }
  }

  // Exact C++ <-> JS catalog parity against core/include/zd/theme.hpp
  const cppHeaderPath = path.resolve(__dirname, '../../core/include/zd/theme.hpp');
  const cppSrc = fs.readFileSync(cppHeaderPath, 'utf8');

  const toastMatch = cppSrc.match(/kThemeToastMs\s*=\s*(\d+)u/);
  assert.ok(toastMatch, 'Expected kThemeToastMs in C++ theme.hpp');
  assert.strictEqual(Number(toastMatch[1]), themes.THEME_TOAST_MS);

  const entryRe =
    /\{ThemeId::(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*\n\s*"([^"]+)",\s*(true|false),\s*\n\s*\{\{(\d+),\s*(\d+),\s*(\d+)\},\s*\{(\d+),\s*(\d+),\s*(\d+)\},\s*\{(\d+),\s*(\d+),\s*(\d+)\},\s*\{(\d+),\s*(\d+),\s*(\d+)\},\s*\{(\d+),\s*(\d+),\s*(\d+)\}\}\}/g;

  const cppEntries = [];
  let m;
  while ((m = entryRe.exec(cppSrc)) !== null) {
    cppEntries.push({
      id: m[1],
      key: m[2],
      name: m[3],
      blurb: m[4],
      lightBackground: m[5] === 'true',
      bg: [Number(m[6]), Number(m[7]), Number(m[8])],
      fg: [Number(m[9]), Number(m[10]), Number(m[11])],
      accent: [Number(m[12]), Number(m[13]), Number(m[14])],
      muted: [Number(m[15]), Number(m[16]), Number(m[17])],
      highlight: [Number(m[18]), Number(m[19]), Number(m[20])]
    });
  }

  assert.strictEqual(
    cppEntries.length,
    themes.THEME_CATALOG.length,
    'C++ kThemeCatalog and JS THEME_CATALOG must have identical entry count'
  );

  for (let i = 0; i < cppEntries.length; i++) {
    const c = cppEntries[i];
    const j = themes.THEME_CATALOG[i];
    assert.strictEqual(j.id, c.id);
    assert.strictEqual(j.key, c.key);
    assert.strictEqual(j.name, c.name);
    assert.strictEqual(j.blurb, c.blurb);
    assert.strictEqual(j.lightBackground, c.lightBackground);
    assert.deepStrictEqual([j.palette.bg.r, j.palette.bg.g, j.palette.bg.b], c.bg);
    assert.deepStrictEqual([j.palette.fg.r, j.palette.fg.g, j.palette.fg.b], c.fg);
    assert.deepStrictEqual([j.palette.accent.r, j.palette.accent.g, j.palette.accent.b], c.accent);
    assert.deepStrictEqual([j.palette.muted.r, j.palette.muted.g, j.palette.muted.b], c.muted);
    assert.deepStrictEqual(
      [j.palette.highlight.r, j.palette.highlight.g, j.palette.highlight.b],
      c.highlight
    );
  }
}

testFunctionalCombinators();
testWifiAnalysisPipeline();
testStateReducerAndPlayfulEvents();
testResponsiveUiAndPixelRasterizer();
testThemesMultiDeviceAndCppParity();
console.log('All Web Port pure functional core, pixel rasterizer & C++ parity tests PASSED!');
