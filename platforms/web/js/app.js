// Imperative Browser Shell for pwnagotchi-zeroday Web Port.
// Bridges DOM controls, HTML5 Canvas, WebAudio buzzer, simulated RF airwaves,
// and the interactive /dev/ttyACM0 serial console onto the pure Functional Core.
(function () {
  'use strict';

  const fp = window.ZD.fp;
  const themes = window.ZD.themes;
  const logic = window.ZD.petLogic;
  const ui = window.ZD.uiModel;
  const sim = window.ZD.airwavesSim;
  const gfxMod = window.ZD.canvasGfx;
  const buzzer = window.ZD.createBuzzer();

  // --- Boot Entropy & Saved Preferences ---
  function getBootEntropy() {
    if (window.crypto && window.crypto.getRandomValues) {
      const arr = new Uint32Array(1);
      window.crypto.getRandomValues(arr);
      return arr[0] >>> 0;
    }
    return Math.floor(Math.random() * 0xFFFFFFFF) >>> 0;
  }

  function loadPref(key, fallback) {
    try {
      const v = window.localStorage.getItem('zd_web_' + key);
      return v !== null ? v : fallback;
    } catch (_) {
      return fallback;
    }
  }

  function savePref(key, val) {
    try {
      window.localStorage.setItem('zd_web_' + key, String(val));
    } catch (_) {
      // Ignore storage errors in private/file contexts
    }
  }

  // --- Mutable Runtime Boundary State ---
  const bootEpochMs = performance.now();
  let gState = logic.initialState(getBootEntropy());
  let simSeed = fp.nextSeed(gState.rngSeed ^ 0xA5A55A5A);

  let displayMode = ui.screenProfileFromKey(loadPref('res', 'plus')) ? loadPref('res', 'plus') : 'plus';
  let activeEnv = sim.PRESETS[loadPref('env', 'cafe')] ? loadPref('env', 'cafe') : 'cafe';
  let customAps = [];

  let scanFinishAtMs = 0;
  let batteryLevel = 96;
  let batteryCharging = true;
  let manualPowerOverride = false;
  let pwnLedUntilMs = 0;

  // --- DOM Elements ---
  const elScreen = document.getElementById('pwn-screen');
  const elViewport = document.getElementById('screen-viewport');
  const elFloatingFx = document.getElementById('floating-fx');
  const elHeaderFace = document.getElementById('header-ascii-face');
  const elStatusLed = document.getElementById('status-led');
  const elLedLabel = document.getElementById('led-label');
  const elDimsBadge = document.getElementById('dims-badge');
  const elEnvIndicator = document.getElementById('env-indicator');

  const elSelectRes = document.getElementById('select-resolution');
  const elSelectShell = document.getElementById('select-shell');
  const elSelectFx = document.getElementById('select-fx');
  const elBtnSfx = document.getElementById('btn-sfx');
  const elBtnSnapPng = document.getElementById('btn-snap-png');
  const elBtnCopyAscii = document.getElementById('btn-copy-ascii');

  const elHwBtnA = document.getElementById('hw-btn-a');
  const elHwBtnB = document.getElementById('hw-btn-b');
  const elBtnBProgress = document.getElementById('btn-b-progress');
  const elBtnThemePrev = document.getElementById('btn-theme-prev');
  const elBtnThemeNext = document.getElementById('btn-theme-next');
  const elActiveThemeLabel = document.getElementById('active-theme-label');
  const elSwatchRow = document.getElementById('swatch-row');

  const elPetNameInput = document.getElementById('input-pet-name');
  const elRankPill = document.getElementById('rank-pill');
  const elXpLabel = document.getElementById('xp-label');
  const elXpFill = document.getElementById('xp-fill');
  const elStatPwns = document.getElementById('stat-pwns');
  const elStatSnacks = document.getElementById('stat-snacks');
  const elStatPeers = document.getElementById('stat-peers');
  const elStatPeak = document.getElementById('stat-peak');

  const elActSnack = document.getElementById('act-snack');
  const elActPwn = document.getElementById('act-pwn');
  const elActPeer = document.getElementById('act-peer');
  const elActScan = document.getElementById('act-scan');

  const elInputBat = document.getElementById('input-battery');
  const elBatVal = document.getElementById('battery-val');
  const elInputChg = document.getElementById('input-charging');

  const elFormBeacon = document.getElementById('form-beacon');
  const elInputSsid = document.getElementById('input-ssid');
  const elBtnRandSsid = document.getElementById('btn-rand-ssid');
  const elInputApCh = document.getElementById('input-ap-ch');
  const elInputApRssi = document.getElementById('input-ap-rssi');
  const elApRssiLabel = document.getElementById('ap-rssi-label');
  const elInputApSec = document.getElementById('input-ap-sec');
  const elBtnClearAps = document.getElementById('btn-clear-aps');
  const elCustomApList = document.getElementById('custom-ap-list');

  const elTermLog = document.getElementById('term-log');
  const elFormTerm = document.getElementById('form-terminal');
  const elInputTerm = document.getElementById('input-terminal');
  const elTermPrompt = document.getElementById('term-prompt-label');
  const elBtnTermAscii = document.getElementById('btn-term-ascii');

  // --- Offscreen Pixel Framebuffer Setup ---
  const displayCtx = elScreen.getContext('2d');
  const offCanvas = document.createElement('canvas');
  const offCtx = offCanvas.getContext('2d');
  let pixelBuf = null;
  let offImageData = null;

  function currentDims() {
    const prof = ui.screenProfileFromKey(displayMode) || ui.screenProfileFromKey('plus');
    const w = prof.dims.width;
    const h = prof.dims.height;
    const scale = Math.max(2, Math.floor(960 / w));
    return { width: w, height: h, scale: scale, monochrome: prof.monochrome, label: prof.label };
  }

  function syncCanvasBuffers() {
    const d = currentDims();
    if (!pixelBuf || pixelBuf.width !== d.width || pixelBuf.height !== d.height) {
      pixelBuf = gfxMod.createPixelBuffer(d.width, d.height);
      offCanvas.width = d.width;
      offCanvas.height = d.height;
      offImageData = offCtx.createImageData(d.width, d.height);

      elScreen.width = d.width * d.scale;
      elScreen.height = d.height * d.scale;
      elDimsBadge.textContent = d.width + '\u00d7' + d.height + (d.monochrome ? ' 1-BIT' : ' RGB565');
    }
  }

  function nowMs() {
    return Math.max(0, Math.floor(performance.now() - bootEpochMs));
  }

  // --- Serial Monitor Logging ---
  function appendLog(tag, message, cssClass) {
    const row = document.createElement('div');
    row.className = 'log-entry' + (cssClass ? ' ' + cssClass : '');

    const tagSpan = document.createElement('span');
    tagSpan.className = 'log-tag';
    tagSpan.textContent = '[' + tag + '] ';

    const textNode = document.createTextNode(message);
    row.appendChild(tagSpan);
    row.appendChild(textNode);

    elTermLog.appendChild(row);
    while (elTermLog.childNodes.length > 140) {
      elTermLog.removeChild(elTermLog.firstChild);
    }
    elTermLog.scrollTop = elTermLog.scrollHeight;
  }

  function logSerialStatus(eventTag, extraClass) {
    const face = logic.faceForMood(gState.mood);
    const spec = themes.themeSpec(gState.theme);
    const msg =
      face.asciiFace +
      ' ' +
      gState.petName +
      '> "' +
      gState.phrase +
      '" | APs:' +
      gState.latestScan.totalAps +
      ' | BusiestCh:' +
      gState.latestScan.busiestChannel +
      ' | Theme:' +
      spec.key;
    appendLog(eventTag, msg, extraClass || '');
  }

  // --- Floating Screen FX Particles ---
  function spawnFloatingGlyphs(glyphs, colorHex) {
    for (let i = 0; i < glyphs.length; i++) {
      setTimeout(function () {
        const span = document.createElement('span');
        span.className = 'float-glyph';
        span.textContent = glyphs[i];
        span.style.left = 18 + Math.random() * 32 + '%';
        span.style.top = 38 + Math.random() * 26 + '%';
        if (colorHex) {
          span.style.color = colorHex;
        }
        elFloatingFx.appendChild(span);
        setTimeout(function () {
          if (span.parentNode) span.parentNode.removeChild(span);
        }, 980);
      }, i * 110);
    }
  }

  // --- Sync Companion Dashboard DOM ---
  function syncDashboardUi() {
    const face = logic.faceForMood(gState.mood);
    elHeaderFace.textContent = face.asciiFace;
    elTermPrompt.textContent = gState.petName + '@ttyACM0:~$';

    if (document.activeElement !== elPetNameInput) {
      elPetNameInput.value = gState.petName;
    }

    const lvl = logic.deriveLevelInfo(gState.xp);
    elRankPill.textContent = 'Lv.' + lvl.level + ' ' + lvl.title;
    elXpLabel.textContent =
      lvl.progressPct >= 100 && lvl.xp >= 600
        ? lvl.xp + ' XP (MAX)'
        : lvl.xp + ' / ' + lvl.nextXp + ' XP';
    elXpFill.style.width = lvl.progressPct + '%';

    elStatPwns.textContent = String(gState.toyHandshakes);
    elStatSnacks.textContent = String(gState.snacksFed);
    elStatPeers.textContent = String(gState.peersMet);
    elStatPeak.textContent = String(gState.peakApCount);

    // Status LED
    const t = nowMs();
    elStatusLed.classList.toggle('scanning', gState.isScanning);
    elStatusLed.classList.toggle('pwn', !gState.isScanning && t < pwnLedUntilMs);
    if (gState.isScanning) {
      elLedLabel.textContent = 'SWEEPING CH 1-13...';
    } else if (t < pwnLedUntilMs) {
      elLedLabel.textContent = 'EAPOL TOY EVENT!';
    } else {
      const preset = sim.PRESETS[activeEnv] || sim.PRESETS.cafe;
      elLedLabel.textContent = gState.isCharging ? 'CHG \u2022 2.4GHZ SIM' : 'BAT \u2022 2.4GHZ SIM';
      const subLabel =
        activeEnv === 'roam'
          ? 'ROAM (' + sim.resolveRoamSubPreset(t).toUpperCase() + ')'
          : preset.label.toUpperCase();
      elEnvIndicator.textContent = 'ENV: ' + subLabel;
    }

    // Active View Segment Buttons
    const pageBtns = document.querySelectorAll('[data-page]');
    pageBtns.forEach(function (btn) {
      const isSelected = btn.getAttribute('data-page') === gState.page;
      btn.classList.toggle('active', isSelected);
      btn.setAttribute('aria-selected', String(isSelected));
    });

    // Active Theme Swatches & Theme Cycler Label
    const activeSpec = themes.themeSpec(gState.theme);
    const activeIdx = Math.max(0, themes.themeIndex(gState.theme));
    if (elActiveThemeLabel) {
      elActiveThemeLabel.textContent =
        activeSpec.name + ' \u2022 ' + (activeIdx + 1) + '/' + themes.THEME_COUNT;
      elActiveThemeLabel.title = activeSpec.blurb;
    }
    const swatches = document.querySelectorAll('[data-theme]');
    swatches.forEach(function (sw) {
      const isPressed = sw.getAttribute('data-theme') === gState.theme;
      sw.classList.toggle('active', isPressed);
      sw.setAttribute('aria-pressed', String(isPressed));
    });

    // Active Environment Cards
    const envCards = document.querySelectorAll('[data-env]');
    envCards.forEach(function (card) {
      const isPressed = card.getAttribute('data-env') === activeEnv;
      card.classList.toggle('active', isPressed);
      card.setAttribute('aria-pressed', String(isPressed));
    });
  }

  // --- Render Pixel Frame onto Canvas ---
  function renderCurrentState() {
    syncCanvasBuffers();
    const d = currentDims();
    const t = nowMs();

    // Subtle natural blink for 140ms every ~4.6s when idle
    const blinkCycle = t % 4600;
    const isBlinking = !gState.isScanning && blinkCycle > 4450;

    const frame = ui.buildUiFrame(gState, { width: d.width, height: d.height });
    gfxMod.paintUiFrame(pixelBuf, frame, { blink: isBlinking });

    offImageData.data.set(pixelBuf.data);
    offCtx.putImageData(offImageData, 0, 0);

    displayCtx.imageSmoothingEnabled = false;
    displayCtx.drawImage(offCanvas, 0, 0, elScreen.width, elScreen.height);

    syncDashboardUi();
  }

  // --- Core Actions ---
  function triggerButtonA() {
    gState = logic.reduce(gState, { type: 'ButtonAPressed', nowMs: nowMs() });
    buzzer.clickA();
    logSerialStatus('BTN_A');
    renderCurrentState();
  }

  function triggerButtonBShort() {
    gState = logic.reduce(gState, { type: 'ButtonBPressed', nowMs: nowMs() });
    buzzer.clickB();
    logSerialStatus('BTN_B');
    checkAndRunScanCycle();
    renderCurrentState();
  }

  function persistCurrentTheme() {
    savePref('theme', themes.themeSpec(gState.theme).key);
  }

  function triggerThemeCycle(step) {
    const dir = step === undefined ? 1 : step;
    gState = logic.reduce(gState, { type: 'CycleTheme', nowMs: nowMs(), step: dir });
    persistCurrentTheme();
    buzzer.themeSwap();
    logSerialStatus('THEME');
    renderCurrentState();
  }

  function triggerPetBoop() {
    gState = logic.reduce(gState, { type: 'PetPoked', nowMs: nowMs() });
    buzzer.boop();
    spawnFloatingGlyphs(['\u2665', '*', '~'], '#ff5ca8');
    logSerialStatus('BOOP');
    renderCurrentState();
  }

  function triggerFeedSnack() {
    gState = logic.reduce(gState, { type: 'SnackFed', nowMs: nowMs() });
    buzzer.boop();
    spawnFloatingGlyphs(['+10XP', '0xFF', '\u2665'], '#38ff7a');
    logSerialStatus('SNACK', 'log-cmd');
    renderCurrentState();
  }

  function triggerToyHandshake() {
    const t = nowMs();
    const target =
      gState.latestScan.strongestSsid ||
      (customAps.length > 0 ? customAps[0].ssid : 'Gibson_Mainframe');
    gState = logic.reduce(gState, {
      type: 'ToyHandshake',
      nowMs: t,
      targetSsid: target
    });
    pwnLedUntilMs = t + 2000;
    buzzer.pwn();
    spawnFloatingGlyphs(['PWN!', 'EAPOL', '+25XP'], '#00e5ff');
    logSerialStatus('TOY_PWN', 'log-pwn');
    renderCurrentState();
  }

  function triggerPeerEncounter(explicitName) {
    const t = nowMs();
    const enc = sim.randomPeerEncounter(simSeed);
    simSeed = enc.nextSeed;
    const peerName = explicitName ? logic.sanitizePetName(explicitName) : enc.peerName;
    gState = logic.reduce(gState, {
      type: 'PeerSpotted',
      nowMs: t,
      peerName,
      peerRssi: enc.peerRssi
    });
    pwnLedUntilMs = t + 1800;
    buzzer.peer();
    spawnFloatingGlyphs(['(^_^)', peerName, '+20XP'], '#ffb020');
    logSerialStatus('PEER', 'log-warn');
    renderCurrentState();
  }

  function switchEnvironment(envId) {
    if (!sim.PRESETS[envId]) return false;
    activeEnv = envId;
    savePref('env', envId);
    appendLog('RF_SIM', 'Switched airspace preset -> ' + sim.PRESETS[envId].label, 'log-cmd');
    if (!gState.isScanning) {
      gState = logic.reduce(gState, { type: 'ButtonBPressed', nowMs: nowMs() });
      checkAndRunScanCycle();
    }
    renderCurrentState();
    return true;
  }

  function renderCustomApTags() {
    elCustomApList.innerHTML = '';
    customAps.forEach(function (ap, idx) {
      const tag = document.createElement('span');
      tag.className = 'ap-tag';
      const lock = ap.secure ? '*' : '';
      tag.textContent = lock + ap.ssid + ' (c' + ui.twoDigit(ap.channel) + ' ' + ap.rssi + 'dB)';

      const rmBtn = document.createElement('button');
      rmBtn.type = 'button';
      rmBtn.textContent = '\u00d7';
      rmBtn.title = 'Stop broadcasting ' + ap.ssid;
      rmBtn.addEventListener('click', function () {
        customAps.splice(idx, 1);
        renderCustomApTags();
        if (!gState.isScanning) {
          gState = logic.reduce(gState, { type: 'ButtonBPressed', nowMs: nowMs() });
          checkAndRunScanCycle();
        }
      });

      tag.appendChild(rmBtn);
      elCustomApList.appendChild(tag);
    });
  }

  function injectCustomAp(ssidRaw, rssiVal, chVal, isSecure) {
    const cleanSsid = window.ZD.wifiAnalysis.sanitizePrintable(ssidRaw);
    const rssi = fp.clampVal(Math.trunc(Number(rssiVal) || -45), -95, -28);
    const channel = fp.clampVal(Math.trunc(Number(chVal) || 6), 1, 13);
    customAps.unshift({
      ssid: cleanSsid === '<hidden>' && String(ssidRaw).trim() !== '' ? 'Custom_AP' : cleanSsid,
      rssi,
      channel,
      secure: Boolean(isSecure),
      enabled: true
    });
    if (customAps.length > 8) {
      customAps.pop();
    }
    renderCustomApTags();
    appendLog(
      'BEACON_TX',
      'Broadcasting virtual SSID "' +
        customAps[0].ssid +
        '" on CH ' +
        channel +
        ' (' +
        rssi +
        ' dBm)',
      'log-cmd'
    );
    if (!gState.isScanning) {
      gState = logic.reduce(gState, { type: 'ButtonBPressed', nowMs: nowMs() });
      checkAndRunScanCycle();
    }
    renderCurrentState();
  }

  // --- Non-Blocking Simulated Scan Loop ---
  function checkAndRunScanCycle() {
    const t = nowMs();

    if (gState.isScanning && t >= scanFinishAtMs) {
      const snap = sim.generateAirwaveSnapshot(activeEnv, customAps, simSeed, t);
      simSeed = snap.nextSeed;
      gState = logic.reduce(gState, {
        type: 'ScanCompleted',
        nowMs: t,
        rawNetworks: snap.networks
      });
      buzzer.scanDone(gState.latestScan.totalAps);
      logSerialStatus(gState.latestScan.totalAps > 0 ? 'SCAN_DONE' : 'SCAN_EMPTY');
    }

    if (logic.shouldTriggerScan(gState, t)) {
      gState = logic.reduce(gState, { type: 'ScanStarted', nowMs: t });
      scanFinishAtMs = t + 1350; // ~3 cute left/right eye sweeps
      logSerialStatus('SCAN_START');
    }
  }

  // --- Wire Hardware Buttons (including 700ms Long-Press on Button B) ---
  elHwBtnA.addEventListener('click', triggerButtonA);

  let btnBDownAt = 0;
  let btnBLongFired = false;
  let btnBHoldTimer = null;

  function clearBtnBHold() {
    btnBDownAt = 0;
    elHwBtnB.classList.remove('pressed');
    elBtnBProgress.style.width = '0%';
    if (btnBHoldTimer) {
      clearInterval(btnBHoldTimer);
      btnBHoldTimer = null;
    }
  }

  elHwBtnB.addEventListener('pointerdown', function (ev) {
    ev.preventDefault();
    btnBDownAt = performance.now();
    btnBLongFired = false;
    elHwBtnB.classList.add('pressed');
    elBtnBProgress.style.width = '0%';

    if (btnBHoldTimer) clearInterval(btnBHoldTimer);
    btnBHoldTimer = setInterval(function () {
      if (!btnBDownAt) return;
      const held = performance.now() - btnBDownAt;
      const pct = Math.min(100, Math.round((held / 700) * 100));
      elBtnBProgress.style.width = pct + '%';
      if (held >= 700 && !btnBLongFired) {
        btnBLongFired = true;
        clearBtnBHold();
        triggerThemeCycle();
      }
    }, 30);
  });

  window.addEventListener('pointerup', function () {
    if (!btnBDownAt) return;
    const wasLong = btnBLongFired;
    clearBtnBHold();
    if (!wasLong) {
      triggerButtonBShort();
    }
  });
  window.addEventListener('pointercancel', clearBtnBHold);

  // --- Direct LCD Screen Touch / Boop ---
  elViewport.addEventListener('click', function (ev) {
    const rect = elScreen.getBoundingClientRect();
    const d = currentDims();
    const lx = ((ev.clientX - rect.left) / rect.width) * d.width;
    const ly = ((ev.clientY - rect.top) / rect.height) * d.height;

    // Clicking bottom status bar triggers [A] or [B]
    if (ly >= d.height - (d.width < 200 ? 14 : 18)) {
      if (lx > d.width * 0.72) {
        triggerButtonBShort();
      } else {
        triggerButtonA();
      }
      return;
    }

    // Clicking ZeroDay's face on PetFace page boops the pet!
    if (gState.page === logic.ScreenPage.PetFace && lx <= d.width * 0.48) {
      triggerPetBoop();
    } else {
      triggerButtonA();
    }
  });

  // --- Toolbar & Companion Controls ---
  elSelectRes.value = displayMode;
  elSelectRes.addEventListener('change', function () {
    const prof = ui.screenProfileFromKey(elSelectRes.value) || ui.screenProfileFromKey('plus');
    displayMode = prof.key;
    savePref('res', displayMode);
    appendLog('DISPLAY', 'Switched profile -> ' + prof.label);
    renderCurrentState();
  });

  const savedShell = loadPref('shell', 'orange');
  document.body.setAttribute('data-shell', savedShell);
  elSelectShell.value = savedShell;
  elSelectShell.addEventListener('change', function () {
    document.body.setAttribute('data-shell', elSelectShell.value);
    savePref('shell', elSelectShell.value);
  });

  const savedFx = loadPref('fx', 'grid');
  elViewport.setAttribute('data-fx', savedFx);
  elSelectFx.value = savedFx;
  elSelectFx.addEventListener('change', function () {
    elViewport.setAttribute('data-fx', elSelectFx.value);
    savePref('fx', elSelectFx.value);
  });

  elBtnSfx.addEventListener('click', function () {
    const on = buzzer.toggle();
    elBtnSfx.textContent = on ? 'SFX: ON' : 'SFX: OFF';
    elBtnSfx.classList.toggle('active', on);
    elBtnSfx.setAttribute('aria-pressed', String(on));
  });

  elBtnSnapPng.addEventListener('click', function () {
    renderCurrentState();
    const link = document.createElement('a');
    link.download = 'pwnagotchi-' + gState.petName + '.png';
    link.href = elScreen.toDataURL('image/png');
    link.click();
    appendLog('SNAPSHOT', 'Saved crisp pixel PNG -> ' + link.download, 'log-cmd');
  });

  function copyAsciiFrameToClipboard() {
    const d = currentDims();
    const frame = ui.buildUiFrame(gState, { width: d.width, height: d.height });
    const ascii = ui.renderAsciiPreview(frame);
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(ascii).then(function () {
        const prev = elBtnCopyAscii.textContent;
        elBtnCopyAscii.textContent = 'Copied!';
        setTimeout(function () {
          elBtnCopyAscii.textContent = prev;
        }, 1200);
      });
    }
    appendLog('ASCII_FRAME', '\n' + ascii.trimEnd());
  }

  elBtnCopyAscii.addEventListener('click', copyAsciiFrameToClipboard);
  elBtnTermAscii.addEventListener('click', copyAsciiFrameToClipboard);

  // Page & Theme quick buttons
  document.querySelectorAll('[data-page]').forEach(function (btn) {
    btn.addEventListener('click', function () {
      gState = logic.reduce(gState, {
        type: 'SetPage',
        nowMs: nowMs(),
        page: btn.getAttribute('data-page')
      });
      buzzer.clickA();
      renderCurrentState();
    });
  });

  // Build theme swatches dynamically from the single-source-of-truth THEME_CATALOG
  if (elSwatchRow) {
    elSwatchRow.innerHTML = '';
    themes.THEME_CATALOG.forEach(function (spec, idx) {
      const sw = document.createElement('button');
      sw.type = 'button';
      const isInitialActive = spec.id === gState.theme;
      sw.className = 'theme-swatch' + (isInitialActive ? ' active' : '');
      sw.setAttribute('data-theme', spec.id);
      sw.setAttribute('aria-pressed', String(isInitialActive));
      sw.style.background =
        'linear-gradient(135deg, ' +
        spec.palette.bg.hex +
        ' 46%, ' +
        spec.palette.fg.hex +
        ' 46%, ' +
        spec.palette.fg.hex +
        ' 76%, ' +
        spec.palette.accent.hex +
        ' 76%)';
      sw.title =
        (idx + 1) + '/' + themes.THEME_COUNT + ' ' + spec.name + ' [' + spec.key + '] — ' + spec.blurb;
      sw.setAttribute('aria-label', spec.name);
      sw.addEventListener('click', function () {
        gState = logic.reduce(gState, {
          type: 'SetTheme',
          nowMs: nowMs(),
          theme: spec.id
        });
        persistCurrentTheme();
        buzzer.themeSwap();
        logSerialStatus('THEME');
        renderCurrentState();
      });
      elSwatchRow.appendChild(sw);
    });
  }

  if (elBtnThemePrev) {
    elBtnThemePrev.addEventListener('click', function () {
      triggerThemeCycle(-1);
    });
  }
  if (elBtnThemeNext) {
    elBtnThemeNext.addEventListener('click', function () {
      triggerThemeCycle(+1);
    });
  }

  // Environment cards
  document.querySelectorAll('[data-env]').forEach(function (card) {
    card.addEventListener('click', function () {
      buzzer.clickA();
      switchEnvironment(card.getAttribute('data-env'));
    });
  });

  // Pet handle rename
  const savedName = loadPref('petName', '');
  if (savedName) {
    gState = logic.reduce(gState, { type: 'PetRenamed', nowMs: 0, petName: savedName });
    gState = Object.freeze(Object.assign({}, gState, { phrase: 'Hello world! Ready to scan.' }));
  }
  const savedTheme = loadPref('theme', '');
  const resolvedBootTheme = themes.themeFromKey(savedTheme);
  if (resolvedBootTheme) {
    gState = logic.reduce(gState, {
      type: 'SetTheme',
      nowMs: 0,
      theme: resolvedBootTheme,
      announce: false
    });
  }

  elPetNameInput.addEventListener('change', function () {
    gState = logic.reduce(gState, {
      type: 'PetRenamed',
      nowMs: nowMs(),
      petName: elPetNameInput.value
    });
    savePref('petName', gState.petName);
    logSerialStatus('RENAME', 'log-cmd');
    renderCurrentState();
  });

  // Toy buttons
  elActSnack.addEventListener('click', triggerFeedSnack);
  elActPwn.addEventListener('click', triggerToyHandshake);
  elActPeer.addEventListener('click', function () {
    triggerPeerEncounter();
  });
  elActScan.addEventListener('click', triggerButtonBShort);

  // Battery controls & optional browser Battery API bridge
  elInputBat.addEventListener('input', function () {
    manualPowerOverride = true;
    batteryLevel = Number(elInputBat.value);
    elBatVal.textContent = batteryLevel + '%';
  });

  elInputChg.addEventListener('change', function () {
    manualPowerOverride = true;
    batteryCharging = elInputChg.checked;
  });

  if (navigator.getBattery) {
    navigator
      .getBattery()
      .then(function (bat) {
        function syncHostBat() {
          if (manualPowerOverride) return;
          batteryLevel = Math.round(bat.level * 100);
          batteryCharging = Boolean(bat.charging);
          elInputBat.value = String(batteryLevel);
          elBatVal.textContent = batteryLevel + '%';
          elInputChg.checked = batteryCharging;
        }
        syncHostBat();
        bat.addEventListener('levelchange', syncHostBat);
        bat.addEventListener('chargingchange', syncHostBat);
      })
      .catch(function () {});
  }

  // Custom Beacon Injector Form
  elInputApRssi.addEventListener('input', function () {
    elApRssiLabel.textContent = elInputApRssi.value + 'dB';
  });

  elBtnRandSsid.addEventListener('click', function () {
    const rolled = sim.randomHackerBeacon(simSeed);
    simSeed = rolled.nextSeed;
    elInputSsid.value = rolled.beacon.ssid;
    elInputApCh.value = String(rolled.beacon.channel);
    elInputApRssi.value = String(rolled.beacon.rssi);
    elApRssiLabel.textContent = rolled.beacon.rssi + 'dB';
    elInputApSec.checked = rolled.beacon.secure;
  });

  elFormBeacon.addEventListener('submit', function (ev) {
    ev.preventDefault();
    let ssid = elInputSsid.value.trim();
    if (!ssid) {
      const rolled = sim.randomHackerBeacon(simSeed);
      simSeed = rolled.nextSeed;
      ssid = rolled.beacon.ssid;
    }
    injectCustomAp(ssid, elInputApRssi.value, elInputApCh.value, elInputApSec.checked);
    elInputSsid.value = '';
  });

  elBtnClearAps.addEventListener('click', function () {
    customAps = [];
    renderCustomApTags();
    appendLog('BEACON_TX', 'Cleared all custom fake beacons.');
    if (!gState.isScanning) {
      gState = logic.reduce(gState, { type: 'ButtonBPressed', nowMs: nowMs() });
      checkAndRunScanCycle();
    }
  });

  // --- Interactive /dev/ttyACM0 Terminal Shell ---
  const cmdHistory = [];
  let cmdHistIdx = -1;

  function runTerminalCommand(rawLine) {
    const trimmed = rawLine.trim();
    if (!trimmed) return;

    appendLog('SHELL', '$ ' + trimmed, 'log-cmd');
    const parts = trimmed.split(/\s+/);
    const cmd = parts[0].toLowerCase();
    const arg1 = parts[1] || '';

    switch (cmd) {
      case 'help':
      case '?':
        appendLog(
          'HELP',
          'Commands: scan | view [next|prev] | theme [next|prev|<key>] | themes | size <micro|compact|plus|epaper|wide> | env <cafe|con|lab|roam|quiet|bunker> | pwn | peer [name] | snack | boop | name <handle> | inject <ssid> [rssi] [ch] | ascii | status | clear'
        );
        break;

      case 'scan':
        triggerButtonBShort();
        break;

      case 'view':
      case 'page':
        if (arg1 && (arg1.toLowerCase() === 'prev' || arg1.toLowerCase() === 'back')) {
          gState = logic.reduce(gState, { type: 'CyclePage', nowMs: nowMs(), step: -1 });
          buzzer.clickA();
          logSerialStatus('BTN_A');
          renderCurrentState();
        } else {
          triggerButtonA();
        }
        break;

      case 'themes': {
        const rows = themes.THEME_CATALOG.map(function (t) {
          const mark = t.id === gState.theme ? '*' : ' ';
          return mark + ' ' + t.key.padEnd(9, ' ') + ' ' + t.name.padEnd(12, ' ') + ' — ' + t.blurb;
        });
        appendLog('THEMES', '\n' + rows.join('\n'), 'log-cmd');
        break;
      }

      case 'theme': {
        const sub = arg1.toLowerCase();
        if (!sub || sub === 'next' || sub === '+') {
          triggerThemeCycle(+1);
        } else if (sub === 'prev' || sub === 'back' || sub === '-') {
          triggerThemeCycle(-1);
        } else {
          const target = themes.themeFromKey(sub);
          if (target) {
            gState = logic.reduce(gState, {
              type: 'SetTheme',
              nowMs: nowMs(),
              theme: target
            });
            persistCurrentTheme();
            buzzer.themeSwap();
            logSerialStatus('THEME');
            renderCurrentState();
          } else {
            appendLog(
              'ERR',
              'Unknown theme "' +
                arg1 +
                '". Keys: ' +
                themes.THEME_CATALOG.map(function (t) {
                  return t.key;
                }).join(', '),
              'log-warn'
            );
          }
        }
        break;
      }

      case 'size':
      case 'res': {
        const prof = ui.screenProfileFromKey(arg1.toLowerCase());
        if (prof) {
          displayMode = prof.key;
          elSelectRes.value = displayMode;
          savePref('res', displayMode);
          appendLog('DISPLAY', 'Switched profile -> ' + prof.label);
          renderCurrentState();
        } else {
          appendLog('ERR', 'Choose size: micro, compact, plus, epaper, wide', 'log-warn');
        }
        break;
      }

      case 'env':
        if (!switchEnvironment(arg1.toLowerCase())) {
          appendLog('ERR', 'Choose env: cafe, con, lab, roam, quiet, bunker', 'log-warn');
        }
        break;

      case 'pwn':
      case 'handshake':
        triggerToyHandshake();
        break;

      case 'peer':
      case 'fren':
        triggerPeerEncounter(arg1);
        break;

      case 'snack':
      case 'feed':
      case 'cookie':
        triggerFeedSnack();
        break;

      case 'pet':
      case 'boop':
      case 'poke':
        triggerPetBoop();
        break;

      case 'name':
        if (!arg1) {
          appendLog('ERR', 'Usage: name <new_handle>', 'log-warn');
        } else {
          gState = logic.reduce(gState, { type: 'PetRenamed', nowMs: nowMs(), petName: arg1 });
          savePref('petName', gState.petName);
          renderCurrentState();
        }
        break;

      case 'inject':
        if (!arg1) {
          appendLog('ERR', 'Usage: inject <ssid> [rssi=-44] [ch=6] [open|wpa2]', 'log-warn');
        } else {
          const rssi = parts[2] ? Number(parts[2]) : -44;
          const ch = parts[3] ? Number(parts[3]) : 6;
          const sec = (parts[4] || 'wpa2').toLowerCase() !== 'open';
          injectCustomAp(arg1, rssi, ch, sec);
        }
        break;

      case 'ascii':
        copyAsciiFrameToClipboard();
        break;

      case 'status':
      case 'whoami': {
        const lvl = logic.deriveLevelInfo(gState.xp);
        appendLog(
          'STATUS',
          gState.petName +
            ' | Lv.' +
            lvl.level +
            ' ' +
            lvl.title +
            ' (' +
            lvl.xp +
            ' XP) | Scans:' +
            gState.completedScans +
            ' | ToyPwns:' +
            gState.toyHandshakes +
            ' | Peers:' +
            gState.peersMet
        );
        break;
      }

      case 'clear':
        elTermLog.innerHTML = '';
        break;

      default:
        appendLog('ERR', 'Unknown command "' + cmd + '". Type "help" for options.', 'log-warn');
        break;
    }
  }

  elFormTerm.addEventListener('submit', function (ev) {
    ev.preventDefault();
    const val = elInputTerm.value;
    if (val.trim()) {
      cmdHistory.unshift(val);
      if (cmdHistory.length > 40) cmdHistory.pop();
    }
    cmdHistIdx = -1;
    elInputTerm.value = '';
    runTerminalCommand(val);
  });

  elInputTerm.addEventListener('keydown', function (ev) {
    if (ev.key === 'ArrowUp') {
      ev.preventDefault();
      if (cmdHistIdx + 1 < cmdHistory.length) {
        cmdHistIdx++;
        elInputTerm.value = cmdHistory[cmdHistIdx];
      }
    } else if (ev.key === 'ArrowDown') {
      ev.preventDefault();
      if (cmdHistIdx > 0) {
        cmdHistIdx--;
        elInputTerm.value = cmdHistory[cmdHistIdx];
      } else {
        cmdHistIdx = -1;
        elInputTerm.value = '';
      }
    }
  });

  // --- Global Hardware Keyboard Shortcuts ---
  window.addEventListener('keydown', function (ev) {
    if (ev.ctrlKey || ev.metaKey || ev.altKey) return;
    const tag = ev.target && ev.target.tagName ? ev.target.tagName.toLowerCase() : '';
    if (tag === 'input' || tag === 'textarea' || tag === 'select') return;

    const k = ev.key.toLowerCase();
    if (k === 'a') {
      ev.preventDefault();
      elHwBtnA.classList.add('pressed');
      setTimeout(function () {
        elHwBtnA.classList.remove('pressed');
      }, 120);
      triggerButtonA();
    } else if (k === 'b') {
      ev.preventDefault();
      triggerButtonBShort();
    } else if (k === 't') {
      ev.preventDefault();
      triggerThemeCycle(ev.shiftKey ? -1 : +1);
    } else if (k === 'f') {
      ev.preventDefault();
      triggerFeedSnack();
    } else if (k === 'h') {
      ev.preventDefault();
      triggerToyHandshake();
    } else if (k === 'p') {
      ev.preventDefault();
      triggerPeerEncounter();
    }
  });

  // --- Main 20 FPS Firmware Loop (matching src/main.cpp 50ms pacing) ---
  logSerialStatus('BOOT');
  appendLog(
    'WEB_TOY',
    'Welcome to pwnagotchi-zeroday Web! Click [A], press [B] to scan, or type "help" below.',
    'log-cmd'
  );
  renderCurrentState();

  setInterval(function () {
    const t = nowMs();
    checkAndRunScanCycle();
    gState = logic.reduce(gState, {
      type: 'Tick',
      nowMs: t,
      batteryPercent: batteryLevel,
      isCharging: batteryCharging
    });
    renderCurrentState();
  }, 60);
})();
