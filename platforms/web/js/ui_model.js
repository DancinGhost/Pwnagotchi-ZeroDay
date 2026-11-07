// Pure responsive UI View-Model projection & ASCII preview (1:1 port of core/include/zd/ui_model.hpp).
(function (root) {
  'use strict';

  const fp = (root.ZD && root.ZD.fp) || (typeof require === 'function' ? require('./fp.js') : null);
  const wifi =
    (root.ZD && root.ZD.wifiAnalysis) ||
    (typeof require === 'function' ? require('./wifi_analysis.js') : null);
  const themes =
    (root.ZD && root.ZD.themes) ||
    (typeof require === 'function' ? require('./themes.js') : null);
  const logic =
    (root.ZD && root.ZD.petLogic) ||
    (typeof require === 'function' ? require('./pet_logic.js') : null);

  const SCREEN_PROFILES = Object.freeze([
    Object.freeze({
      key: 'micro',
      label: '128×64 OLED (SSD1306)',
      dims: Object.freeze({ width: 128, height: 64 }),
      monochrome: true
    }),
    Object.freeze({
      key: 'compact',
      label: '160×80 (StickC Classic)',
      dims: Object.freeze({ width: 160, height: 80 }),
      monochrome: false
    }),
    Object.freeze({
      key: 'plus',
      label: '240×135 (StickC Plus2)',
      dims: Object.freeze({ width: 240, height: 135 }),
      monochrome: false
    }),
    Object.freeze({
      key: 'epaper',
      label: '250×122 (2.13" e-Paper)',
      dims: Object.freeze({ width: 250, height: 122 }),
      monochrome: true
    }),
    Object.freeze({
      key: 'wide',
      label: '320×240 (Core2 / CYD)',
      dims: Object.freeze({ width: 320, height: 240 }),
      monochrome: false
    })
  ]);

  function screenProfileFromKey(key) {
    for (let i = 0; i < SCREEN_PROFILES.length; i++) {
      if (SCREEN_PROFILES[i].key === key) {
        return SCREEN_PROFILES[i];
      }
    }
    return null;
  }

  const rgb565 = themes.rgb565;
  const resolvePalette = themes.resolvePalette;

  function twoDigit(val) {
    const clamped = Math.abs(Math.trunc(val)) % 100;
    return String(clamped).padStart(2, '0');
  }

  function formatUptime(uptimeMs, compact) {
    const totalSec = Math.floor(Math.max(0, uptimeMs) / 1000);
    const secs = totalSec % 60;
    const mins = Math.floor(totalSec / 60) % 60;
    const hours = Math.floor(totalSec / 3600) % 100;

    if (compact) {
      if (hours > 0) {
        return twoDigit(hours) + 'h' + twoDigit(mins);
      }
      return twoDigit(mins) + ':' + twoDigit(secs);
    }
    return twoDigit(hours) + ':' + twoDigit(mins) + ':' + twoDigit(secs);
  }

  function truncateText(text, maxChars) {
    const s = String(text || '');
    if (maxChars <= 0) return '';
    if (s.length <= maxChars) return s;
    if (maxChars <= 2) return s.slice(0, maxChars);
    return s.slice(0, maxChars - 2) + '..';
  }

  function wrapWords(text, maxCols, maxLines) {
    const s = String(text || '');
    if (maxCols <= 0 || maxLines <= 0 || s.length === 0) {
      return [];
    }

    const words = s.trim().split(/\s+/).filter(Boolean);
    const lines = [];
    let currentLine = '';

    for (let i = 0; i < words.length; i++) {
      const rawWord = words[i];
      const clippedWord = rawWord.length > maxCols ? truncateText(rawWord, maxCols) : rawWord;

      if (currentLine.length === 0) {
        currentLine = clippedWord;
      } else if (currentLine.length + 1 + clippedWord.length <= maxCols) {
        currentLine += ' ' + clippedWord;
      } else {
        lines.push(currentLine);
        if (lines.length >= maxLines) {
          currentLine = '';
          break;
        }
        currentLine = clippedWord;
      }
    }

    if (currentLine.length > 0 && lines.length < maxLines) {
      lines.push(currentLine);
    }

    return lines;
  }

  function buildThemeToast(state, dims, compact, bodyTop, bodyBottom) {
    if (!themes.isThemeToastVisible(state.themeToastUntilMs || 0, state.uptimeMs || 0)) {
      return Object.freeze({
        visible: false,
        text: '',
        x: 0,
        y: 0,
        w: 0,
        h: 0,
        textX: 0,
        textY: 0
      });
    }

    const label = themes.themeLabel(state.theme);
    const idx = Math.max(0, themes.themeIndex(state.theme));
    const position = idx + 1 + '/' + themes.THEME_COUNT;
    const maxChars = Math.max(8, Math.floor((dims.width - 16) / 6));
    const text = truncateText(
      compact ? '< ' + label + ' >' : 'THEME ' + position + ': ' + label,
      maxChars
    );

    const padX = compact ? 5 : 8;
    const padY = compact ? 4 : 6;
    const w = text.length * 6 + padX * 2;
    const h = 8 + padY * 2;
    const x = Math.trunc((dims.width - w) / 2);
    const y = bodyTop + Math.trunc((bodyBottom - bodyTop - h) / 2);

    return Object.freeze({
      visible: true,
      text,
      x,
      y,
      w,
      h,
      textX: x + padX,
      textY: y + padY
    });
  }

  function buildUiFrame(state, dimsInput) {
    const dims = {
      width: Math.max(96, dimsInput && dimsInput.width ? Math.trunc(dimsInput.width) : 240),
      height: Math.max(56, dimsInput && dimsInput.height ? Math.trunc(dimsInput.height) : 135)
    };
    const micro = dims.width < 150 || dims.height < 72;
    const compact = dims.width < 200 || dims.height < 100;
    const wide = dims.width >= 300 && dims.height >= 180;
    const palette = resolvePalette(state.theme);
    const face = logic.faceForMood(state.mood);

    const headerH = micro ? 10 : compact ? 12 : 17;
    const footerH = micro ? 10 : compact ? 12 : 17;
    const headerDivY = headerH;
    const footerDivY = dims.height - footerH - 1;
    const maxFrameCols = Math.max(12, Math.floor((dims.width - 4) / 6));

    const chText = state.isScanning
      ? 'CH:*'
      : state.latestScan.busiestChannel > 0
        ? 'CH:' + twoDigit(state.latestScan.busiestChannel)
        : 'CH:--';

    const apText = 'AP:' + state.latestScan.totalAps;
    const upText = 'UP:' + formatUptime(state.uptimeMs, compact);
    const batPrefix = state.isCharging ? '+' : '';
    const batText = batPrefix + state.batteryPercent + '%';

    let headerLine;
    if (micro) {
      headerLine = chText + ' ' + apText + ' ' + batText;
    } else if (compact) {
      headerLine = chText + ' ' + apText + ' ' + formatUptime(state.uptimeMs, true) + ' ' + batText;
    } else {
      const peakPart = '(' + state.peakApCount + ')';
      headerLine = chText + '  ' + apText + peakPart + '  ' + upText + '  ' + batText;
    }
    headerLine = truncateText(headerLine, maxFrameCols);

    let footerLine;
    if (micro) {
      const statusPart = state.isScanning ? 'SCN..' : '#' + twoDigit(state.completedScans);
      const rssiPart =
        state.latestScan.totalAps > 0 ? state.latestScan.strongestRssi + 'dB' : '--dB';
      footerLine = statusPart + ' ' + rssiPart + ' [A]/[B]';
    } else if (compact) {
      const statusPart = state.isScanning ? 'SCAN..' : '#' + twoDigit(state.completedScans);
      const rssiPart =
        state.latestScan.totalAps > 0 ? state.latestScan.strongestRssi + 'dB' : '--dB';
      footerLine = statusPart + ' ' + rssiPart + ' [A]VW [B]SCN';
    } else {
      const statusPart = state.isScanning
        ? 'SCANNING..'
        : 'SCAN #' + twoDigit(state.completedScans);
      const rssiPart =
        state.latestScan.totalAps > 0
          ? 'BEST ' + state.latestScan.strongestRssi + 'dBm'
          : 'BEST --dBm';
      footerLine = statusPart + '  ' + rssiPart + '  [A]PG [B]SCN';
    }
    footerLine = truncateText(footerLine, maxFrameCols);

    const bodyTop = headerDivY + 1;
    const bodyBottom = footerDivY - 1;
    const bodyH = bodyBottom - bodyTop;

    let geom;
    let speechX;
    let speechTopY;
    let lineStepY;
    let speechCols;
    let speechMaxLines;

    if (micro) {
      const centerY = bodyTop + Math.trunc(bodyH / 2) - 4;
      geom = {
        centerX: 28,
        centerY,
        eyeOffsetX: 9,
        eyeY: centerY - 2,
        eyeRadius: 4,
        mouthY: centerY + 5,
        mouthRadius: 3,
        bracketOffsetX: 21,
        bracketHalfHeight: 10,
        moodBadgeY: bodyBottom - 8
      };
      speechX = 56;
      speechTopY = bodyTop + 2;
      lineStepY = 9;
      speechCols = Math.max(6, Math.floor((dims.width - speechX - 2) / 6));
      speechMaxLines = Math.max(2, Math.floor((bodyH - 12) / 9));
    } else if (compact) {
      const centerY = bodyTop + Math.trunc(bodyH / 2) - 4;
      geom = {
        centerX: 35,
        centerY,
        eyeOffsetX: 11,
        eyeY: centerY - 3,
        eyeRadius: 5,
        mouthY: centerY + 6,
        mouthRadius: 4,
        bracketOffsetX: 26,
        bracketHalfHeight: 13,
        moodBadgeY: bodyBottom - 9
      };
      speechX = 70;
      speechTopY = bodyTop + 3;
      lineStepY = 10;
      speechCols = Math.floor((dims.width - speechX - 3) / 6);
      speechMaxLines = 4;
    } else if (wide) {
      const centerY = bodyTop + Math.trunc(bodyH / 2) - 8;
      geom = {
        centerX: 72,
        centerY,
        eyeOffsetX: 22,
        eyeY: centerY - 5,
        eyeRadius: 10,
        mouthY: centerY + 13,
        mouthRadius: 8,
        bracketOffsetX: 54,
        bracketHalfHeight: 26,
        moodBadgeY: bodyBottom - 14
      };
      speechX = 144;
      speechTopY = bodyTop + 8;
      lineStepY = 14;
      speechCols = Math.floor((dims.width - speechX - 6) / 6);
      speechMaxLines = Math.max(5, Math.floor((bodyH - 38) / 14));
    } else {
      const centerY = bodyTop + Math.trunc(bodyH / 2) - 7;
      geom = {
        centerX: 55,
        centerY,
        eyeOffsetX: 18,
        eyeY: centerY - 4,
        eyeRadius: 8,
        mouthY: centerY + 10,
        mouthRadius: 6,
        bracketOffsetX: 42,
        bracketHalfHeight: 20,
        moodBadgeY: bodyBottom - 12
      };
      speechX = 112;
      speechTopY = bodyTop + 6;
      lineStepY = 12;
      speechCols = Math.floor((dims.width - speechX - 4) / 6);
      speechMaxLines = bodyH >= 92 ? 5 : 4;
    }

    const promptLabel = truncateText(state.petName + '>', speechCols);
    const wrappedPhrase = wrapWords(state.phrase, speechCols, speechMaxLines);

    let spotlightLabel = '';
    if (!compact && state.latestScan.totalAps > 0 && state.latestScan.strongestSsid.length > 0) {
      const rawSpot =
        '> ' + state.latestScan.strongestSsid + ' (' + state.latestScan.strongestRssi + ')';
      spotlightLabel = truncateText(rawSpot, speechCols);
    }

    const radarRowStep = compact ? 12 : 15;
    const maxRadarRows = Math.max(2, Math.floor((bodyH - (compact ? 4 : 5)) / radarRowStep));
    const maxSsidChars = Math.max(
      6,
      Math.floor((dims.width - (micro ? 80 : compact ? 88 : 126)) / 6)
    );
    const visibleAps = fp.take(state.latestScan.topNetworks, maxRadarRows);
    const radarRows = fp.map(visibleAps, function (ap) {
      const chStr = ap.channel > 0 ? 'c' + twoDigit(ap.channel) : 'c--';
      const details = chStr + ' ' + ap.rssi;
      return {
        ssid: truncateText(ap.ssid, maxSsidChars),
        details,
        bars: wifi.rssiToBars(ap.rssi),
        secure: ap.secure
      };
    });

    const radarEmptyHint = truncateText(
      state.isScanning
        ? 'Scanning nearby WiFi...'
        : state.hasScannedOnce
          ? 'No WiFi APs in range.'
          : 'Waiting for first scan...',
      maxFrameCols
    );

    const chartBaseY = bodyBottom - (micro ? 9 : compact ? 11 : 14);
    const chartMaxBarH = Math.max(
      14,
      chartBaseY - bodyTop - (micro ? 12 : compact ? 16 : 29)
    );

    let maxChCount = 1;
    for (let ch = 1; ch <= 13; ch++) {
      if (state.latestScan.histogram.counts[ch] > maxChCount) {
        maxChCount = state.latestScan.histogram.counts[ch];
      }
    }

    const channelBars = [];
    for (let idx = 0; idx < 13; idx++) {
      const c = state.latestScan.histogram.counts[idx + 1] || 0;
      const barH =
        c === 0 ? 0 : Math.max(2, Math.trunc((c * chartMaxBarH) / maxChCount));
      channelBars.push({
        channel: idx + 1,
        count: c,
        heightPx: barH
      });
    }

    const spectrumSummary = compact
      ? 'SEC:' + state.latestScan.secureAps + ' OPN:' + state.latestScan.openAps
      : '2.4GHz CH 1-13 | SEC:' +
        state.latestScan.secureAps +
        ' OPEN:' +
        state.latestScan.openAps;

    const toast = buildThemeToast(state, dims, compact, bodyTop, bodyBottom);

    return Object.freeze({
      dims,
      compact,
      palette,
      page: state.page,
      headerDividerY: headerDivY,
      footerDividerY: footerDivY,
      headerTextY: micro ? 1 : compact ? 2 : 5,
      footerTextY: footerDivY + (micro ? 2 : compact ? 3 : 5),
      headerLine,
      footerLine,
      face,
      faceGeom: geom,
      speechX,
      speechTopY,
      lineStepY,
      promptLabel,
      phraseLines: wrappedPhrase,
      spotlightLabel,
      radarRows,
      radarEmptyHint,
      channelBars,
      chartBaseY,
      chartMaxBarH,
      spectrumSummary,
      theme: state.theme,
      toast
    });
  }

  function renderAsciiPreview(frame) {
    const innerW =
      frame.dims.width === 160 ? 28 : Math.max(22, Math.floor(frame.dims.width / 6));
    const padLine = function (content) {
      const clipped = truncateText(content, innerW);
      return '| ' + clipped + ' '.repeat(innerW - clipped.length) + ' |\n';
    };
    const border = '+-' + '-'.repeat(innerW) + '-+\n';

    let out = border;
    out += padLine(frame.headerLine);
    out += border;

    if (frame.toast && frame.toast.visible) {
      out += padLine('** ' + frame.toast.text + ' **');
    }

    if (frame.page === logic.ScreenPage.PetFace) {
      out += padLine(frame.face.asciiFace + '  ' + frame.promptLabel);
      for (let i = 0; i < frame.phraseLines.length; i++) {
        out += padLine('       ' + frame.phraseLines[i]);
      }
      out += padLine('[' + frame.face.moodLabel + '] ' + frame.spotlightLabel);
    } else if (frame.page === logic.ScreenPage.NearbyRadar) {
      out += padLine('NEARBY WIFI RADAR');
      if (frame.radarRows.length === 0) {
        out += padLine(frame.radarEmptyHint);
      } else {
        for (let i = 0; i < frame.radarRows.length; i++) {
          const row = frame.radarRows[i];
          const lockChar = row.secure ? '*' : ' ';
          out += padLine(lockChar + row.ssid + ' ' + row.details);
        }
      }
    } else {
      out += padLine(frame.spectrumSummary);
      const countsRow =
        'CH:' +
        frame.channelBars
          .map(function (b) {
            return String(b.count);
          })
          .join(' ');
      out += padLine(countsRow);
    }

    out += border;
    out += padLine(frame.footerLine);
    out += border;
    return out;
  }

  const uiModel = Object.freeze({
    SCREEN_PROFILES,
    screenProfileFromKey,
    rgb565,
    resolvePalette,
    twoDigit,
    formatUptime,
    truncateText,
    wrapWords,
    buildThemeToast,
    buildUiFrame,
    renderAsciiPreview
  });

  root.ZD = root.ZD || {};
  root.ZD.uiModel = uiModel;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = uiModel;
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
