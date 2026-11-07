// Crisp pixel-buffer M5GFX software rasterizer + 5x7 Font0 bitmap glyphs
// (ported directly from src/main.cpp so the webpage canvas matches M5Stick pixel-for-pixel).
(function (root) {
  'use strict';

  const logic =
    (root.ZD && root.ZD.petLogic) ||
    (typeof require === 'function' ? require('./pet_logic.js') : null);

  // Classic 5x7 GLCD / Adafruit GFX Font0 printable ASCII (0x20 ' ' .. 0x7E '~'), 5 bytes/char.
  // Bit 0 (0x01) = top pixel row, Bit 6 (0x40) = bottom pixel row, Bit 7 (0x80) = descender.
  const FONT_5X7 = [
    [0x00, 0x00, 0x00, 0x00, 0x00], // 32 ' '
    [0x00, 0x00, 0x5F, 0x00, 0x00], // 33 '!'
    [0x00, 0x07, 0x00, 0x07, 0x00], // 34 '"'
    [0x14, 0x7F, 0x14, 0x7F, 0x14], // 35 '#'
    [0x24, 0x2A, 0x7F, 0x2A, 0x12], // 36 '$'
    [0x23, 0x13, 0x08, 0x64, 0x62], // 37 '%'
    [0x36, 0x49, 0x55, 0x22, 0x50], // 38 '&'
    [0x00, 0x05, 0x03, 0x00, 0x00], // 39 '''
    [0x00, 0x1C, 0x22, 0x41, 0x00], // 40 '('
    [0x00, 0x41, 0x22, 0x1C, 0x00], // 41 ')'
    [0x14, 0x08, 0x3E, 0x08, 0x14], // 42 '*'
    [0x08, 0x08, 0x3E, 0x08, 0x08], // 43 '+'
    [0x00, 0x50, 0x30, 0x00, 0x00], // 44 ','
    [0x08, 0x08, 0x08, 0x08, 0x08], // 45 '-'
    [0x00, 0x60, 0x60, 0x00, 0x00], // 46 '.'
    [0x20, 0x10, 0x08, 0x04, 0x02], // 47 '/'
    [0x3E, 0x51, 0x49, 0x45, 0x3E], // 48 '0'
    [0x00, 0x42, 0x7F, 0x40, 0x00], // 49 '1'
    [0x42, 0x61, 0x51, 0x49, 0x46], // 50 '2'
    [0x21, 0x41, 0x45, 0x4B, 0x31], // 51 '3'
    [0x18, 0x14, 0x12, 0x7F, 0x10], // 52 '4'
    [0x27, 0x45, 0x45, 0x45, 0x39], // 53 '5'
    [0x3C, 0x4A, 0x49, 0x49, 0x30], // 54 '6'
    [0x01, 0x71, 0x09, 0x05, 0x03], // 55 '7'
    [0x36, 0x49, 0x49, 0x49, 0x36], // 56 '8'
    [0x06, 0x49, 0x49, 0x29, 0x1E], // 57 '9'
    [0x00, 0x36, 0x36, 0x00, 0x00], // 58 ':'
    [0x00, 0x56, 0x36, 0x00, 0x00], // 59 ';'
    [0x08, 0x14, 0x22, 0x41, 0x00], // 60 '<'
    [0x14, 0x14, 0x14, 0x14, 0x14], // 61 '='
    [0x00, 0x41, 0x22, 0x14, 0x08], // 62 '>'
    [0x02, 0x01, 0x51, 0x09, 0x06], // 63 '?'
    [0x32, 0x49, 0x79, 0x41, 0x3E], // 64 '@'
    [0x7E, 0x11, 0x11, 0x11, 0x7E], // 65 'A'
    [0x7F, 0x49, 0x49, 0x49, 0x36], // 66 'B'
    [0x3E, 0x41, 0x41, 0x41, 0x22], // 67 'C'
    [0x7F, 0x41, 0x41, 0x22, 0x1C], // 68 'D'
    [0x7F, 0x49, 0x49, 0x49, 0x41], // 69 'E'
    [0x7F, 0x09, 0x09, 0x09, 0x01], // 70 'F'
    [0x3E, 0x41, 0x49, 0x49, 0x7A], // 71 'G'
    [0x7F, 0x08, 0x08, 0x08, 0x7F], // 72 'H'
    [0x00, 0x41, 0x7F, 0x41, 0x00], // 73 'I'
    [0x20, 0x40, 0x41, 0x3F, 0x01], // 74 'J'
    [0x7F, 0x08, 0x14, 0x22, 0x41], // 75 'K'
    [0x7F, 0x40, 0x40, 0x40, 0x40], // 76 'L'
    [0x7F, 0x02, 0x0C, 0x02, 0x7F], // 77 'M'
    [0x7F, 0x04, 0x08, 0x10, 0x7F], // 78 'N'
    [0x3E, 0x41, 0x41, 0x41, 0x3E], // 79 'O'
    [0x7F, 0x09, 0x09, 0x09, 0x06], // 80 'P'
    [0x3E, 0x41, 0x51, 0x21, 0x5E], // 81 'Q'
    [0x7F, 0x09, 0x19, 0x29, 0x46], // 82 'R'
    [0x46, 0x49, 0x49, 0x49, 0x31], // 83 'S'
    [0x01, 0x01, 0x7F, 0x01, 0x01], // 84 'T'
    [0x3F, 0x40, 0x40, 0x40, 0x3F], // 85 'U'
    [0x1F, 0x20, 0x40, 0x20, 0x1F], // 86 'V'
    [0x3F, 0x40, 0x38, 0x40, 0x3F], // 87 'W'
    [0x63, 0x14, 0x08, 0x14, 0x63], // 88 'X'
    [0x07, 0x08, 0x70, 0x08, 0x07], // 89 'Y'
    [0x61, 0x51, 0x49, 0x45, 0x43], // 90 'Z'
    [0x00, 0x7F, 0x41, 0x41, 0x00], // 91 '['
    [0x02, 0x04, 0x08, 0x10, 0x20], // 92 '\'
    [0x00, 0x41, 0x41, 0x7F, 0x00], // 93 ']'
    [0x04, 0x02, 0x01, 0x02, 0x04], // 94 '^'
    [0x40, 0x40, 0x40, 0x40, 0x40], // 95 '_'
    [0x00, 0x01, 0x02, 0x04, 0x00], // 96 '`'
    [0x20, 0x54, 0x54, 0x54, 0x78], // 97 'a'
    [0x7F, 0x48, 0x44, 0x44, 0x38], // 98 'b'
    [0x38, 0x44, 0x44, 0x44, 0x20], // 99 'c'
    [0x38, 0x44, 0x44, 0x48, 0x7F], // 100 'd'
    [0x38, 0x54, 0x54, 0x54, 0x18], // 101 'e'
    [0x08, 0x7E, 0x09, 0x01, 0x02], // 102 'f'
    [0x0C, 0x52, 0x52, 0x52, 0x3E], // 103 'g'
    [0x7F, 0x08, 0x04, 0x04, 0x78], // 104 'h'
    [0x00, 0x44, 0x7D, 0x40, 0x00], // 105 'i'
    [0x20, 0x40, 0x44, 0x3D, 0x00], // 106 'j'
    [0x7F, 0x10, 0x28, 0x44, 0x00], // 107 'k'
    [0x00, 0x41, 0x7F, 0x40, 0x00], // 108 'l'
    [0x7C, 0x04, 0x18, 0x04, 0x78], // 109 'm'
    [0x7C, 0x08, 0x04, 0x04, 0x78], // 110 'n'
    [0x38, 0x44, 0x44, 0x44, 0x38], // 111 'o'
    [0x7C, 0x14, 0x14, 0x14, 0x08], // 112 'p'
    [0x08, 0x14, 0x14, 0x18, 0x7C], // 113 'q'
    [0x7C, 0x08, 0x04, 0x04, 0x08], // 114 'r'
    [0x48, 0x54, 0x54, 0x54, 0x20], // 115 's'
    [0x04, 0x3F, 0x44, 0x40, 0x20], // 116 't'
    [0x3C, 0x40, 0x40, 0x20, 0x7C], // 117 'u'
    [0x1C, 0x20, 0x40, 0x20, 0x1C], // 118 'v'
    [0x3C, 0x40, 0x30, 0x40, 0x3C], // 119 'w'
    [0x44, 0x28, 0x10, 0x28, 0x44], // 120 'x'
    [0x0C, 0x50, 0x50, 0x50, 0x3C], // 121 'y'
    [0x44, 0x64, 0x54, 0x4C, 0x44], // 122 'z'
    [0x00, 0x08, 0x36, 0x41, 0x00], // 123 '{'
    [0x00, 0x00, 0x7F, 0x00, 0x00], // 124 '|'
    [0x00, 0x41, 0x36, 0x08, 0x00], // 125 '}'
    [0x08, 0x04, 0x08, 0x10, 0x08]  // 126 '~'
  ];

  // Headless-safe RGBA pixel buffer mimicking M5Canvas / M5GFX primitives.
  function createPixelBuffer(width, height) {
    const w = Math.max(1, Math.trunc(width));
    const h = Math.max(1, Math.trunc(height));
    const data = new Uint8ClampedArray(w * h * 4);
    let textFg = { r: 255, g: 255, b: 255 };
    let textBg = null;

    function setPixel(x, y, col) {
      const ix = Math.trunc(x);
      const iy = Math.trunc(y);
      if (ix < 0 || ix >= w || iy < 0 || iy >= h) return;
      const idx = (iy * w + ix) * 4;
      data[idx] = col.r;
      data[idx + 1] = col.g;
      data[idx + 2] = col.b;
      data[idx + 3] = 255;
    }

    function fillScreen(col) {
      for (let i = 0; i < data.length; i += 4) {
        data[i] = col.r;
        data[i + 1] = col.g;
        data[i + 2] = col.b;
        data[i + 3] = 255;
      }
    }

    function fillRect(x, y, rw, rh, col) {
      const x0 = Math.max(0, Math.trunc(x));
      const y0 = Math.max(0, Math.trunc(y));
      const x1 = Math.min(w, Math.trunc(x) + Math.trunc(rw));
      const y1 = Math.min(h, Math.trunc(y) + Math.trunc(rh));
      for (let py = y0; py < y1; py++) {
        let idx = (py * w + x0) * 4;
        for (let px = x0; px < x1; px++) {
          data[idx] = col.r;
          data[idx + 1] = col.g;
          data[idx + 2] = col.b;
          data[idx + 3] = 255;
          idx += 4;
        }
      }
    }

    function drawLine(x0In, y0In, x1In, y1In, col) {
      let x0 = Math.trunc(x0In);
      let y0 = Math.trunc(y0In);
      const x1 = Math.trunc(x1In);
      const y1 = Math.trunc(y1In);

      const dx = Math.abs(x1 - x0);
      const sx = x0 < x1 ? 1 : -1;
      const dy = -Math.abs(y1 - y0);
      const sy = y0 < y1 ? 1 : -1;
      let err = dx + dy;

      while (true) {
        setPixel(x0, y0, col);
        if (x0 === x1 && y0 === y1) break;
        const e2 = 2 * err;
        if (e2 >= dy) {
          err += dy;
          x0 += sx;
        }
        if (e2 <= dx) {
          err += dx;
          y0 += sy;
        }
      }
    }

    function fillCircle(cxIn, cyIn, rIn, col) {
      const cx = Math.trunc(cxIn);
      const cy = Math.trunc(cyIn);
      const r = Math.max(0, Math.trunc(rIn));
      if (r === 0) {
        setPixel(cx, cy, col);
        return;
      }
      const limitSq = r * r + r * 0.4;
      for (let dy = -r; dy <= r; dy++) {
        for (let dx = -r; dx <= r; dx++) {
          if (dx * dx + dy * dy <= limitSq) {
            setPixel(cx + dx, cy + dy, col);
          }
        }
      }
    }

    function fillRoundRect(xIn, yIn, rwIn, rhIn, radiusIn, col) {
      const x = Math.trunc(xIn);
      const y = Math.trunc(yIn);
      const rw = Math.trunc(rwIn);
      const rh = Math.trunc(rhIn);
      const rad = Math.max(0, Math.min(Math.trunc(radiusIn), Math.floor(Math.min(rw, rh) / 2)));
      if (rad <= 0) {
        fillRect(x, y, rw, rh, col);
        return;
      }
      fillRect(x + rad, y, rw - 2 * rad, rh, col);
      fillRect(x, y + rad, rad, rh - 2 * rad, col);
      fillRect(x + rw - rad, y + rad, rad, rh - 2 * rad, col);
      fillCircle(x + rad, y + rad, rad, col);
      fillCircle(x + rw - rad - 1, y + rad, rad, col);
      fillCircle(x + rad, y + rh - rad - 1, rad, col);
      fillCircle(x + rw - rad - 1, y + rh - rad - 1, rad, col);
    }

    // M5GFX fillArc convention: 0 deg = +X (right), 90 deg = +Y (down),
    // 180 deg = -X (left), 270 deg = -Y (up), sweeping clockwise deg0 -> deg1.
    function fillArc(cxIn, cyIn, r0In, r1In, deg0In, deg1In, col) {
      const cx = Math.trunc(cxIn);
      const cy = Math.trunc(cyIn);
      const rOuter = Math.max(r0In, r1In);
      const rInner = Math.max(0, Math.min(r0In, r1In));
      const d0 = ((deg0In % 360) + 360) % 360;
      const d1 = ((deg1In % 360) + 360) % 360;
      const wraps = d0 > d1;

      const bound = Math.ceil(rOuter) + 1;
      for (let dy = -bound; dy <= bound; dy++) {
        for (let dx = -bound; dx <= bound; dx++) {
          const dist = Math.hypot(dx, dy);
          const inRadius =
            rInner === 0
              ? dist <= rOuter + 0.3
              : dist > rInner + 0.05 && dist <= rOuter + 0.28;
          if (!inRadius) continue;

          if (dx === 0 && dy === 0) {
            if (rInner === 0) setPixel(cx, cy, col);
            continue;
          }

          const deg = ((Math.atan2(dy, dx) * 180) / Math.PI + 360) % 360;
          const inAngle = wraps ? deg >= d0 || deg <= d1 : deg >= d0 && deg <= d1;
          if (inAngle) {
            setPixel(cx + dx, cy + dy, col);
          }
        }
      }
    }

    function setTextColor(fg, bg) {
      textFg = fg;
      textBg = bg || null;
    }

    function drawChar(code, x, y) {
      const idx = code >= 32 && code <= 126 ? code - 32 : 31; // fallback '?'
      const cols = FONT_5X7[idx];
      if (textBg) {
        fillRect(x, y, 6, 8, textBg);
      }
      for (let cx = 0; cx < 5; cx++) {
        const mask = cols[cx];
        if (mask === 0) continue;
        for (let cy = 0; cy < 8; cy++) {
          if ((mask & (1 << cy)) !== 0) {
            setPixel(x + cx, y + cy, textFg);
          }
        }
      }
    }

    function drawString(text, xIn, yIn) {
      const str = String(text == null ? '' : text);
      let cursorX = Math.trunc(xIn);
      const cursorY = Math.trunc(yIn);
      for (let i = 0; i < str.length; i++) {
        drawChar(str.charCodeAt(i), cursorX, cursorY);
        cursorX += 6;
      }
    }

    return {
      width: w,
      height: h,
      data,
      setPixel,
      fillScreen,
      fillRect,
      drawLine,
      fillCircle,
      fillRoundRect,
      fillArc,
      setTextColor,
      drawString
    };
  }

  // --- Exact Pwnagotchi-ZeroDay UI Painting Primitives (ported from src/main.cpp) ---

  function drawFaceBrackets(gfx, g, color, compact) {
    const r = g.bracketHalfHeight + (compact ? 4 : 6);
    const thickness = compact ? 2 : 3;
    const leftArcCenterX = g.centerX - g.bracketOffsetX + r;
    const rightArcCenterX = g.centerX + g.bracketOffsetX - r;

    gfx.fillArc(leftArcCenterX, g.centerY, r, r - thickness, 142.0, 218.0, color);
    gfx.fillArc(rightArcCenterX, g.centerY, r, r - thickness, 322.0, 38.0, color);
  }

  function drawSingleEye(gfx, cx, cy, r, style, pal, compact, isRightEye) {
    const stroke = compact ? 2 : 3;

    switch (style) {
      case logic.EyeGlyph.RoundCenter: {
        gfx.fillCircle(cx, cy, r, pal.fg);
        gfx.fillCircle(
          cx - Math.trunc(r / 3),
          cy - Math.trunc(r / 3),
          Math.max(1, Math.trunc(r / 4)),
          pal.bg
        );
        break;
      }
      case logic.EyeGlyph.RoundLeft: {
        const shift = compact ? -2 : -4;
        gfx.fillCircle(cx + shift, cy, r, pal.fg);
        gfx.fillCircle(
          cx + shift - Math.trunc(r / 3),
          cy - Math.trunc(r / 4),
          Math.max(1, Math.trunc(r / 4)),
          pal.bg
        );
        break;
      }
      case logic.EyeGlyph.RoundRight: {
        const shift = compact ? 2 : 4;
        gfx.fillCircle(cx + shift, cy, r, pal.fg);
        gfx.fillCircle(
          cx + shift + Math.trunc(r / 3),
          cy - Math.trunc(r / 4),
          Math.max(1, Math.trunc(r / 4)),
          pal.bg
        );
        break;
      }
      case logic.EyeGlyph.HappyArc: {
        gfx.fillArc(cx, cy + Math.trunc(r / 3), r, r - stroke, 200.0, 340.0, pal.fg);
        break;
      }
      case logic.EyeGlyph.WideSparkle: {
        gfx.fillCircle(cx, cy, r + 1, pal.fg);
        gfx.fillCircle(cx, cy, Math.max(2, r - stroke), pal.bg);
        gfx.fillCircle(cx, cy, Math.max(1, Math.trunc(r / 3)), pal.fg);
        break;
      }
      case logic.EyeGlyph.Sunglasses: {
        const w = r * 2 + (compact ? 2 : 4);
        const h = r + (compact ? 3 : 5);
        gfx.fillRoundRect(cx - Math.trunc(w / 2), cy - Math.trunc(h / 2), w, h, 2, pal.fg);
        gfx.drawLine(
          cx - Math.trunc(w / 4),
          cy - Math.trunc(h / 2) + 2,
          cx - Math.trunc(w / 2) + 2,
          cy + Math.trunc(h / 4),
          pal.bg
        );
        break;
      }
      case logic.EyeGlyph.SadDroop: {
        gfx.fillCircle(cx, cy, r - 1, pal.fg);
        gfx.fillCircle(cx, cy - 1, Math.max(1, Math.trunc(r / 3)), pal.bg);
        if (!isRightEye) {
          gfx.drawLine(cx - r, cy - r - 1, cx + r, cy - r - 3, pal.fg);
        } else {
          gfx.drawLine(cx - r, cy - r - 3, cx + r, cy - r - 1, pal.fg);
        }
        break;
      }
      case logic.EyeGlyph.HalfLid: {
        gfx.fillCircle(cx, cy, r, pal.fg);
        gfx.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, r, pal.bg);
        gfx.fillRect(cx - r, cy - 1, r * 2 + 1, stroke, pal.fg);
        break;
      }
      case logic.EyeGlyph.SleepingLine: {
        gfx.fillArc(cx, cy - Math.trunc(r / 3), r, r - stroke, 25.0, 155.0, pal.fg);
        break;
      }
      case logic.EyeGlyph.CyberWink: {
        // Left eye winks '>', right eye sparkles '*'
        if (!isRightEye) {
          const span = Math.max(3, r - 1);
          for (let t = 0; t < stroke; t++) {
            gfx.drawLine(cx - span, cy - span + t, cx + span - 1, cy + t, pal.fg);
            gfx.drawLine(cx - span, cy + span - t, cx + span - 1, cy - t, pal.fg);
          }
        } else {
          gfx.fillCircle(cx, cy, r + 1, pal.fg);
          gfx.fillCircle(cx, cy, Math.max(2, r - stroke), pal.bg);
          gfx.fillCircle(cx, cy, Math.max(1, Math.trunc(r / 3)), pal.highlight);
        }
        break;
      }
    }
  }

  function drawMouth(gfx, g, mouth, pal, compact) {
    const stroke = 2;
    const r = g.mouthRadius;

    switch (mouth) {
      case logic.MouthGlyph.DoubleSmile: {
        gfx.fillArc(
          g.centerX - r + 1,
          g.mouthY - Math.trunc(r / 2),
          r,
          r - stroke,
          18.0,
          162.0,
          pal.fg
        );
        gfx.fillArc(
          g.centerX + r - 1,
          g.mouthY - Math.trunc(r / 2),
          r,
          r - stroke,
          18.0,
          162.0,
          pal.fg
        );
        break;
      }
      case logic.MouthGlyph.BigGrin: {
        gfx.fillArc(g.centerX, g.mouthY - 2, r + 2, 0, 10.0, 170.0, pal.fg);
        gfx.fillArc(
          g.centerX,
          g.mouthY - 2,
          Math.max(1, r - 1),
          0,
          15.0,
          165.0,
          pal.highlight
        );
        break;
      }
      case logic.MouthGlyph.Smirk: {
        gfx.fillRect(g.centerX - r, g.mouthY, r * 2, stroke, pal.fg);
        gfx.fillArc(g.centerX + r, g.mouthY - 2, 4, 4 - stroke, 15.0, 110.0, pal.fg);
        break;
      }
      case logic.MouthGlyph.FlatLine: {
        gfx.fillRect(g.centerX - r - 1, g.mouthY, (r + 1) * 2, stroke, pal.fg);
        break;
      }
      case logic.MouthGlyph.SadWave: {
        gfx.fillArc(g.centerX, g.mouthY + r, r + 1, r + 1 - stroke, 205.0, 335.0, pal.fg);
        break;
      }
    }
  }

  function canBlinkStyle(style) {
    return (
      style === logic.EyeGlyph.RoundCenter ||
      style === logic.EyeGlyph.WideSparkle ||
      style === logic.EyeGlyph.SadDroop ||
      style === logic.EyeGlyph.HalfLid
    );
  }

  function drawPwnagotchiFace(gfx, frame, opts) {
    const g = frame.faceGeom;
    const pal = frame.palette;
    const f = frame.face;
    const blink = Boolean(opts && opts.blink) && canBlinkStyle(f.eyes);
    const eyeStyle = blink ? logic.EyeGlyph.SleepingLine : f.eyes;

    drawFaceBrackets(gfx, g, pal.fg, frame.compact);

    const leftEyeX = g.centerX - g.eyeOffsetX;
    const rightEyeX = g.centerX + g.eyeOffsetX;

    drawSingleEye(gfx, leftEyeX, g.eyeY, g.eyeRadius, eyeStyle, pal, frame.compact, false);
    drawSingleEye(gfx, rightEyeX, g.eyeY, g.eyeRadius, eyeStyle, pal, frame.compact, true);

    if (eyeStyle === logic.EyeGlyph.Sunglasses) {
      gfx.fillRect(
        leftEyeX + g.eyeRadius,
        g.eyeY - 2,
        rightEyeX - leftEyeX - g.eyeRadius * 2,
        3,
        pal.fg
      );
      const templeW = frame.compact ? 5 : 8;
      gfx.fillRect(leftEyeX - g.eyeRadius - templeW, g.eyeY - 2, templeW, 2, pal.fg);
    }

    if (f.showBlush) {
      const blushR = frame.compact ? 2 : 3;
      gfx.fillCircle(leftEyeX - g.eyeRadius - 1, g.mouthY - 1, blushR, pal.highlight);
      gfx.fillCircle(rightEyeX + g.eyeRadius + 1, g.mouthY - 1, blushR, pal.highlight);
    }

    if (f.showTear) {
      gfx.fillCircle(
        rightEyeX + g.eyeRadius - 1,
        g.eyeY + g.eyeRadius + 3,
        frame.compact ? 1 : 2,
        pal.accent
      );
    }

    if (f.showZzz) {
      gfx.setTextColor(pal.accent, pal.bg);
      gfx.drawString(
        'zZ',
        rightEyeX + g.eyeRadius + 2,
        g.eyeY - g.eyeRadius - (frame.compact ? 5 : 8)
      );
    }

    drawMouth(gfx, g, f.mouth, pal, frame.compact);

    const badge = '[' + f.moodLabel + ']';
    const badgeW = badge.length * 6;
    gfx.setTextColor(pal.muted, pal.bg);
    gfx.drawString(badge, Math.max(2, g.centerX - Math.trunc(badgeW / 2)), g.moodBadgeY);
  }

  function drawSignalMeter(gfx, x, bottomY, bars, pal, compact) {
    const barW = compact ? 2 : 3;
    const gap = 1;
    for (let i = 0; i < 4; i++) {
      const h = (i + 1) * (compact ? 2 : 3);
      const bx = x + i * (barW + gap);
      const by = bottomY - h;
      if (i < bars) {
        gfx.fillRect(bx, by, barW, h, pal.fg);
      } else {
        gfx.fillRect(bx, bottomY - 1, barW, 1, pal.muted);
      }
    }
  }

  function drawPetFacePage(gfx, frame, opts) {
    drawPwnagotchiFace(gfx, frame, opts);

    if (!frame.compact) {
      gfx.drawLine(
        frame.speechX - 6,
        frame.headerDividerY + 5,
        frame.speechX - 6,
        frame.footerDividerY - 5,
        frame.palette.muted
      );
    }

    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.promptLabel, frame.speechX, frame.speechTopY);

    gfx.setTextColor(frame.palette.fg, frame.palette.bg);
    let y = frame.speechTopY + frame.lineStepY + (frame.compact ? 1 : 2);
    for (let i = 0; i < frame.phraseLines.length; i++) {
      gfx.drawString(frame.phraseLines[i], frame.speechX, y);
      y += frame.lineStepY;
    }

    if (frame.spotlightLabel && frame.spotlightLabel.length > 0) {
      gfx.setTextColor(frame.palette.muted, frame.palette.bg);
      gfx.drawString(frame.spotlightLabel, frame.speechX, frame.footerDividerY - 12);
    }
  }

  function drawRadarPage(gfx, frame) {
    if (frame.radarRows.length === 0) {
      gfx.setTextColor(frame.palette.muted, frame.palette.bg);
      const textW = frame.radarEmptyHint.length * 6;
      const cx = Math.max(4, Math.trunc((frame.dims.width - textW) / 2));
      const cy = Math.trunc((frame.headerDividerY + frame.footerDividerY) / 2) - 4;
      gfx.drawString(frame.radarEmptyHint, cx, cy);
      return;
    }

    const rowStep = frame.compact ? 12 : 15;
    let y = frame.headerDividerY + (frame.compact ? 4 : 5);

    for (let i = 0; i < frame.radarRows.length; i++) {
      const row = frame.radarRows[i];
      drawSignalMeter(gfx, 4, y + (frame.compact ? 8 : 9), row.bars, frame.palette, frame.compact);

      const textX = frame.compact ? 19 : 24;
      const prefix = row.secure ? '*' : ' ';
      gfx.setTextColor(frame.palette.fg, frame.palette.bg);
      gfx.drawString(prefix + row.ssid, textX, y);

      const detailW = row.details.length * 6;
      gfx.setTextColor(frame.palette.accent, frame.palette.bg);
      gfx.drawString(row.details, frame.dims.width - detailW - 4, y);

      y += rowStep;
    }
  }

  function drawSpectrumPage(gfx, frame) {
    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.spectrumSummary, 4, frame.headerDividerY + (frame.compact ? 3 : 5));

    const usableW = frame.dims.width - 12;
    const slotW = Math.trunc(usableW / 13);
    const barW = Math.max(3, slotW - (frame.compact ? 2 : 4));
    const startX = Math.trunc((frame.dims.width - slotW * 13) / 2);

    gfx.drawLine(startX, frame.chartBaseY, startX + slotW * 13, frame.chartBaseY, frame.palette.muted);

    for (let i = 0; i < 13; i++) {
      const bar = frame.channelBars[i];
      const bx = startX + i * slotW + 1;
      if (bar.heightPx > 0) {
        gfx.fillRect(bx, frame.chartBaseY - bar.heightPx, barW, bar.heightPx, frame.palette.fg);
      }
      const showLabel = frame.compact
        ? bar.channel === 1 || bar.channel === 6 || bar.channel === 11 || bar.channel === 13
        : bar.channel % 2 === 1;
      if (showLabel) {
        gfx.setTextColor(frame.palette.muted, frame.palette.bg);
        gfx.drawString(String(bar.channel), bx, frame.chartBaseY + (frame.dims.height < 72 ? 1 : 2));
      }
    }
  }

  function drawThemeToast(gfx, frame) {
    const t = frame.toast;
    if (!t || !t.visible) {
      return;
    }
    const pal = frame.palette;
    gfx.fillRoundRect(t.x - 2, t.y - 2, t.w + 4, t.h + 4, 4, pal.accent);
    gfx.fillRoundRect(t.x, t.y, t.w, t.h, 3, pal.fg);
    gfx.setTextColor(pal.bg, pal.fg);
    gfx.drawString(t.text, t.textX, t.textY);
  }

  function paintUiFrame(gfx, frame, opts) {
    gfx.fillScreen(frame.palette.bg);

    // Top telemetry bar
    gfx.setTextColor(frame.palette.fg, frame.palette.bg);
    gfx.drawString(frame.headerLine, 4, frame.headerTextY);
    gfx.drawLine(0, frame.headerDividerY, frame.dims.width - 1, frame.headerDividerY, frame.palette.muted);

    // Main content page
    switch (frame.page) {
      case logic.ScreenPage.PetFace:
        drawPetFacePage(gfx, frame, opts);
        break;
      case logic.ScreenPage.NearbyRadar:
        drawRadarPage(gfx, frame);
        break;
      case logic.ScreenPage.ChannelSpectrum:
        drawSpectrumPage(gfx, frame);
        break;
    }

    // Bottom status bar
    gfx.drawLine(0, frame.footerDividerY, frame.dims.width - 1, frame.footerDividerY, frame.palette.muted);
    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.footerLine, 4, frame.footerTextY);

    // Transient theme-change toast overlay
    drawThemeToast(gfx, frame);
  }

  const canvasGfx = Object.freeze({
    FONT_5X7,
    createPixelBuffer,
    paintUiFrame
  });

  root.ZD = root.ZD || {};
  root.ZD.canvasGfx = canvasGfx;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = canvasGfx;
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
