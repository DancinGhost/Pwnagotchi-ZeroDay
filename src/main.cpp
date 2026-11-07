// Imperative Hardware Shell for Pwnagotchi-ZeroDay (M5StickC / Plus / Plus2)
// All domain logic, state transitions, WiFi aggregation, and UI layout live in
// the pure functional headers under include/. This file only bridges M5Unified,
// Arduino WiFi, and Serial I/O at the boundary.

#include "ui_model.hpp"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {

// Single mutable state cell at the imperative runtime boundary (Elm/Redux style).
zd::AppState gState = zd::initialState();

M5Canvas gCanvas(&M5.Display);
bool gCanvasReady = false;
bool gBtnBLongHandled = false;
uint32_t gLastRenderMs = 0;

// --- Thin Graphics Primitives Translating Pure UiFrame onto M5GFX / M5Canvas ---

template <typename Gfx>
void drawFaceBrackets(Gfx& gfx, const zd::FaceGeometry& g, uint16_t color, bool compact) {
    const int16_t r = static_cast<int16_t>(g.bracketHalfHeight + (compact ? 4 : 6));
    const int16_t thickness = compact ? 2 : 3;

    const int16_t leftArcCenterX = static_cast<int16_t>(g.centerX - g.bracketOffsetX + r);
    const int16_t rightArcCenterX = static_cast<int16_t>(g.centerX + g.bracketOffsetX - r);

    gfx.fillArc(leftArcCenterX, g.centerY, r, r - thickness, 142.0f, 218.0f, color);
    gfx.fillArc(rightArcCenterX, g.centerY, r, r - thickness, 322.0f, 38.0f, color);
}

template <typename Gfx>
void drawSingleEye(Gfx& gfx,
                   int16_t cx,
                   int16_t cy,
                   int16_t r,
                   zd::EyeGlyph style,
                   const zd::ColorPalette& pal,
                   bool compact) {
    const int16_t stroke = compact ? 2 : 3;

    switch (style) {
        case zd::EyeGlyph::RoundCenter: {
            gfx.fillCircle(cx, cy, r, pal.fg);
            gfx.fillCircle(cx - (r / 3), cy - (r / 3), (std::max)(1, r / 4), pal.bg);
            break;
        }
        case zd::EyeGlyph::RoundLeft: {
            const int16_t shift = compact ? -2 : -4;
            gfx.fillCircle(cx + shift, cy, r, pal.fg);
            gfx.fillCircle(cx + shift - (r / 3), cy - (r / 4), (std::max)(1, r / 4), pal.bg);
            break;
        }
        case zd::EyeGlyph::RoundRight: {
            const int16_t shift = compact ? 2 : 4;
            gfx.fillCircle(cx + shift, cy, r, pal.fg);
            gfx.fillCircle(cx + shift + (r / 3), cy - (r / 4), (std::max)(1, r / 4), pal.bg);
            break;
        }
        case zd::EyeGlyph::HappyArc: {
            gfx.fillArc(cx, cy + (r / 3), r, r - stroke, 200.0f, 340.0f, pal.fg);
            break;
        }
        case zd::EyeGlyph::WideSparkle: {
            gfx.fillCircle(cx, cy, r + 1, pal.fg);
            gfx.fillCircle(cx, cy, (std::max)(2, r - stroke), pal.bg);
            gfx.fillCircle(cx, cy, (std::max)(1, r / 3), pal.fg);
            break;
        }
        case zd::EyeGlyph::Sunglasses: {
            const int16_t w = static_cast<int16_t>(r * 2 + (compact ? 2 : 4));
            const int16_t h = static_cast<int16_t>(r + (compact ? 3 : 5));
            gfx.fillRoundRect(cx - (w / 2), cy - (h / 2), w, h, 2, pal.fg);
            // Diagonal lens glint
            gfx.drawLine(cx - (w / 4), cy - (h / 2) + 2, cx - (w / 2) + 2, cy + (h / 4), pal.bg);
            break;
        }
        case zd::EyeGlyph::SadDroop: {
            gfx.fillCircle(cx, cy, r - 1, pal.fg);
            gfx.fillCircle(cx, cy - 1, (std::max)(1, r / 3), pal.bg);
            // Slanted sad eyebrow
            gfx.drawLine(cx - r, cy - r - 1, cx + r, cy - r - 3, pal.fg);
            break;
        }
        case zd::EyeGlyph::HalfLid: {
            gfx.fillCircle(cx, cy, r, pal.fg);
            gfx.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, r, pal.bg);
            gfx.fillRect(cx - r, cy - 1, r * 2 + 1, stroke, pal.fg);
            break;
        }
        case zd::EyeGlyph::SleepingLine: {
            gfx.fillArc(cx, cy - (r / 3), r, r - stroke, 25.0f, 155.0f, pal.fg);
            break;
        }
    }
}

template <typename Gfx>
void drawMouth(Gfx& gfx, const zd::FaceGeometry& g, zd::MouthGlyph mouth, const zd::ColorPalette& pal, bool compact) {
    const int16_t stroke = compact ? 2 : 2;
    const int16_t r = g.mouthRadius;

    switch (mouth) {
        case zd::MouthGlyph::DoubleSmile: {
            // Classic Pwnagotchi double-curve ‿‿
            gfx.fillArc(g.centerX - r + 1, g.mouthY - (r / 2), r, r - stroke, 18.0f, 162.0f, pal.fg);
            gfx.fillArc(g.centerX + r - 1, g.mouthY - (r / 2), r, r - stroke, 18.0f, 162.0f, pal.fg);
            break;
        }
        case zd::MouthGlyph::BigGrin: {
            gfx.fillArc(g.centerX, g.mouthY - 2, r + 2, 0, 10.0f, 170.0f, pal.fg);
            gfx.fillArc(g.centerX, g.mouthY - 2, (std::max)(1, r - 1), 0, 15.0f, 165.0f, pal.highlight);
            break;
        }
        case zd::MouthGlyph::Smirk: {
            gfx.fillRect(g.centerX - r, g.mouthY, r * 2, stroke, pal.fg);
            gfx.fillArc(g.centerX + r, g.mouthY - 2, 4, 4 - stroke, 15.0f, 110.0f, pal.fg);
            break;
        }
        case zd::MouthGlyph::FlatLine: {
            gfx.fillRect(g.centerX - r - 1, g.mouthY, (r + 1) * 2, stroke, pal.fg);
            break;
        }
        case zd::MouthGlyph::SadWave: {
            gfx.fillArc(g.centerX, g.mouthY + r, r + 1, r + 1 - stroke, 205.0f, 335.0f, pal.fg);
            break;
        }
    }
}

template <typename Gfx>
void drawPwnagotchiFace(Gfx& gfx, const zd::UiFrame& frame) {
    const auto& g = frame.faceGeom;
    const auto& pal = frame.palette;
    const auto& f = frame.face;

    drawFaceBrackets(gfx, g, pal.fg, frame.compact);

    const int16_t leftEyeX = static_cast<int16_t>(g.centerX - g.eyeOffsetX);
    const int16_t rightEyeX = static_cast<int16_t>(g.centerX + g.eyeOffsetX);

    drawSingleEye(gfx, leftEyeX, g.eyeY, g.eyeRadius, f.eyes, pal, frame.compact);
    drawSingleEye(gfx, rightEyeX, g.eyeY, g.eyeRadius, f.eyes, pal, frame.compact);

    // Connect sunglasses bridge and temple arm
    if (f.eyes == zd::EyeGlyph::Sunglasses) {
        gfx.fillRect(leftEyeX + g.eyeRadius, g.eyeY - 2, (rightEyeX - leftEyeX) - (g.eyeRadius * 2), 3, pal.fg);
        gfx.fillRect(leftEyeX - g.eyeRadius - (frame.compact ? 5 : 8), g.eyeY - 2,
                     frame.compact ? 5 : 8, 2, pal.fg);
    }

    if (f.showBlush) {
        const int16_t blushR = frame.compact ? 2 : 3;
        gfx.fillCircle(leftEyeX - g.eyeRadius - 1, g.mouthY - 1, blushR, pal.highlight);
        gfx.fillCircle(rightEyeX + g.eyeRadius + 1, g.mouthY - 1, blushR, pal.highlight);
    }

    if (f.showTear) {
        gfx.fillCircle(rightEyeX + g.eyeRadius - 1, g.eyeY + g.eyeRadius + 3,
                       frame.compact ? 1 : 2, pal.accent);
    }

    if (f.showZzz) {
        gfx.setTextColor(pal.accent, pal.bg);
        gfx.drawString("zZ", rightEyeX + g.eyeRadius + 2, g.eyeY - g.eyeRadius - (frame.compact ? 5 : 8));
    }

    drawMouth(gfx, g, f.mouth, pal, frame.compact);

    // Centered mood tag beneath the face
    const std::string badge = "[" + std::string(f.moodLabel) + "]";
    const int16_t badgeW = static_cast<int16_t>(badge.size() * 6);
    gfx.setTextColor(pal.muted, pal.bg);
    gfx.drawString(badge.c_str(), (std::max)(2, g.centerX - (badgeW / 2)), g.moodBadgeY);
}

template <typename Gfx>
void drawSignalMeter(Gfx& gfx, int16_t x, int16_t bottomY, int bars, const zd::ColorPalette& pal, bool compact) {
    const int16_t barW = compact ? 2 : 3;
    const int16_t gap = 1;
    for (int i = 0; i < 4; ++i) {
        const int16_t h = static_cast<int16_t>((i + 1) * (compact ? 2 : 3));
        const int16_t bx = static_cast<int16_t>(x + i * (barW + gap));
        const int16_t by = static_cast<int16_t>(bottomY - h);
        if (i < bars) {
            gfx.fillRect(bx, by, barW, h, pal.fg);
        } else {
            gfx.fillRect(bx, bottomY - 1, barW, 1, pal.muted);
        }
    }
}

template <typename Gfx>
void drawPetFacePage(Gfx& gfx, const zd::UiFrame& frame) {
    drawPwnagotchiFace(gfx, frame);

    // Subtle vertical divider between face and speech column on full-size displays
    if (!frame.compact) {
        gfx.drawLine(frame.speechX - 6, frame.headerDividerY + 5,
                     frame.speechX - 6, frame.footerDividerY - 5, frame.palette.muted);
    }

    // Pet prompt header ("zeroday>")
    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.promptLabel.c_str(), frame.speechX, frame.speechTopY);

    // Wrapped phrase lines
    gfx.setTextColor(frame.palette.fg, frame.palette.bg);
    int16_t y = static_cast<int16_t>(frame.speechTopY + frame.lineStepY + (frame.compact ? 1 : 2));
    for (const std::string& line : frame.phraseLines) {
        gfx.drawString(line.c_str(), frame.speechX, y);
        y = static_cast<int16_t>(y + frame.lineStepY);
    }

    // Strongest nearby SSID ticker at bottom of speech column (240x135)
    if (!frame.spotlightLabel.empty()) {
        gfx.setTextColor(frame.palette.muted, frame.palette.bg);
        gfx.drawString(frame.spotlightLabel.c_str(), frame.speechX, frame.footerDividerY - 12);
    }
}

template <typename Gfx>
void drawRadarPage(Gfx& gfx, const zd::UiFrame& frame) {
    if (frame.radarRows.empty()) {
        gfx.setTextColor(frame.palette.muted, frame.palette.bg);
        const int16_t textW = static_cast<int16_t>(frame.radarEmptyHint.size() * 6);
        const int16_t cx = (std::max)(4, (frame.dims.width - textW) / 2);
        const int16_t cy = static_cast<int16_t>((frame.headerDividerY + frame.footerDividerY) / 2 - 4);
        gfx.drawString(frame.radarEmptyHint.c_str(), cx, cy);
        return;
    }

    const int16_t rowStep = frame.compact ? 12 : 15;
    int16_t y = static_cast<int16_t>(frame.headerDividerY + (frame.compact ? 4 : 5));

    for (const auto& row : frame.radarRows) {
        drawSignalMeter(gfx, 4, y + (frame.compact ? 8 : 9), row.bars, frame.palette, frame.compact);

        const int16_t textX = frame.compact ? 19 : 24;
        const std::string prefix = row.secure ? "*" : " ";
        gfx.setTextColor(frame.palette.fg, frame.palette.bg);
        gfx.drawString((prefix + row.ssid).c_str(), textX, y);

        const int16_t detailW = static_cast<int16_t>(row.details.size() * 6);
        gfx.setTextColor(frame.palette.accent, frame.palette.bg);
        gfx.drawString(row.details.c_str(), frame.dims.width - detailW - 4, y);

        y = static_cast<int16_t>(y + rowStep);
    }
}

template <typename Gfx>
void drawSpectrumPage(Gfx& gfx, const zd::UiFrame& frame) {
    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.spectrumSummary.c_str(), 4, frame.headerDividerY + (frame.compact ? 3 : 5));

    const int16_t usableW = static_cast<int16_t>(frame.dims.width - 12);
    const int16_t slotW = static_cast<int16_t>(usableW / 13);
    const int16_t barW = (std::max)(3, slotW - (frame.compact ? 2 : 4));
    const int16_t startX = static_cast<int16_t>((frame.dims.width - (slotW * 13)) / 2);

    gfx.drawLine(startX, frame.chartBaseY, startX + slotW * 13, frame.chartBaseY, frame.palette.muted);

    for (std::size_t i = 0; i < 13u; ++i) {
        const auto& bar = frame.channelBars[i];
        const int16_t bx = static_cast<int16_t>(startX + static_cast<int16_t>(i) * slotW + 1);
        if (bar.heightPx > 0) {
            gfx.fillRect(bx, frame.chartBaseY - bar.heightPx, barW, bar.heightPx, frame.palette.fg);
        }
        // Label key channels (1, 6, 11, 13 on compact; odd channels on full display)
        const bool showLabel = frame.compact
                                   ? (bar.channel == 1 || bar.channel == 6 || bar.channel == 11 || bar.channel == 13)
                                   : ((bar.channel % 2) == 1);
        if (showLabel) {
            gfx.setTextColor(frame.palette.muted, frame.palette.bg);
            gfx.drawString(std::to_string(bar.channel).c_str(), bx, frame.chartBaseY + 2);
        }
    }
}

template <typename Gfx>
void paintUiFrame(Gfx& gfx, const zd::UiFrame& frame) {
    gfx.fillScreen(frame.palette.bg);
    gfx.setFont(&fonts::Font0);
    gfx.setTextSize(1);

    // Top telemetry bar
    gfx.setTextColor(frame.palette.fg, frame.palette.bg);
    gfx.drawString(frame.headerLine.c_str(), 4, frame.headerTextY);
    gfx.drawLine(0, frame.headerDividerY, frame.dims.width - 1, frame.headerDividerY, frame.palette.muted);

    // Main content page
    switch (frame.page) {
        case zd::ScreenPage::PetFace:
            drawPetFacePage(gfx, frame);
            break;
        case zd::ScreenPage::NearbyRadar:
            drawRadarPage(gfx, frame);
            break;
        case zd::ScreenPage::ChannelSpectrum:
            drawSpectrumPage(gfx, frame);
            break;
    }

    // Bottom status bar
    gfx.drawLine(0, frame.footerDividerY, frame.dims.width - 1, frame.footerDividerY, frame.palette.muted);
    gfx.setTextColor(frame.palette.accent, frame.palette.bg);
    gfx.drawString(frame.footerLine.c_str(), 4, frame.footerTextY);
}

void renderCurrentState() {
    const zd::ScreenDimensions dims{
        static_cast<int16_t>(M5.Display.width()),
        static_cast<int16_t>(M5.Display.height())
    };
    const zd::UiFrame frame = zd::buildUiFrame(gState, dims);

    if (gCanvasReady) {
        paintUiFrame(gCanvas, frame);
        gCanvas.pushSprite(0, 0);
    } else {
        paintUiFrame(M5.Display, frame);
    }
}

void logSerialStatus(const char* eventTag) {
    const zd::FaceDescriptor face = zd::faceForMood(gState.mood);
    Serial.printf("[%s] %s %s> \"%s\" | APs:%u | BusiestCh:%d\n",
                  eventTag,
                  face.asciiFace,
                  gState.petName.c_str(),
                  gState.phrase.c_str(),
                  static_cast<unsigned>(gState.latestScan.totalAps),
                  static_cast<int>(gState.latestScan.busiestChannel));
}

std::vector<zd::AccessPoint> collectCompletedWifiScan(int16_t count) {
    std::vector<zd::AccessPoint> networks;
    if (count <= 0) {
        return networks;
    }
    networks.reserve(static_cast<std::size_t>(count));
    for (int16_t i = 0; i < count; ++i) {
        networks.push_back(zd::AccessPoint{
            std::string(WiFi.SSID(i).c_str()),
            static_cast<int32_t>(WiFi.RSSI(i)),
            static_cast<int32_t>(WiFi.channel(i)),
            WiFi.encryptionType(i) != WIFI_AUTH_OPEN
        });
    }
    return networks;
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // Landscape orientation (240x135 on StickC Plus/Plus2, 160x80 on StickC)
    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    gCanvas.setColorDepth(16);
    gCanvasReady = gCanvas.createSprite(M5.Display.width(), M5.Display.height());

    // Configure WiFi in standard non-connected station mode for passive/beacon scanning
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);

    // Seed pure functional PRNG with hardware entropy once at startup
    const uint32_t bootEntropy = esp_random();
    gState = zd::initialState(bootEntropy);

    renderCurrentState();
    logSerialStatus("BOOT");
}

void loop() {
    M5.update();
    const uint32_t nowMs = millis();

    // 1. Sample Button A (front M5 button -> cycle display page)
    if (M5.BtnA.wasPressed()) {
        gState = zd::reduce(gState, zd::ButtonAPressedEvent{nowMs});
        logSerialStatus("BTN_A");
    }

    // 2. Sample Button B (short press -> manual scan, 700ms hold -> cycle theme)
    if (M5.BtnB.wasPressed()) {
        gBtnBLongHandled = false;
    }
    if (M5.BtnB.isPressed() && !gBtnBLongHandled && M5.BtnB.pressedFor(700)) {
        gBtnBLongHandled = true;
        gState = zd::reduce(gState, zd::ButtonBLongPressedEvent{nowMs});
        logSerialStatus("THEME");
    }
    if (M5.BtnB.wasReleased() && !gBtnBLongHandled) {
        gState = zd::reduce(gState, zd::ButtonBPressedEvent{nowMs});
        logSerialStatus("BTN_B");
    }

    // 3. Check if an async WiFi scan finished (or timed out)
    if (gState.isScanning) {
        const int16_t scanStatus = WiFi.scanComplete();
        if (scanStatus >= 0) {
            std::vector<zd::AccessPoint> raw = collectCompletedWifiScan(scanStatus);
            WiFi.scanDelete();
            gState = zd::reduce(gState, zd::ScanCompletedEvent{nowMs, std::move(raw)});
            logSerialStatus("SCAN_DONE");
        } else if (scanStatus == WIFI_SCAN_FAILED || (nowMs - gState.lastScanStartMs) > 15000u) {
            WiFi.scanDelete();
            gState = zd::reduce(gState, zd::ScanCompletedEvent{nowMs, {}});
            logSerialStatus("SCAN_EMPTY");
        }
    }

    // 4. Start a new async WiFi scan when requested by pure scheduling predicate
    if (zd::shouldTriggerScan(gState, nowMs)) {
        WiFi.scanDelete();
        const int16_t rc = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
        if (rc == WIFI_SCAN_RUNNING || rc >= 0) {
            gState = zd::reduce(gState, zd::ScanStartedEvent{nowMs});
            logSerialStatus("SCAN_START");
        }
    }

    // 5. Advance pure periodic state tick (battery, eye animation, idle mood)
    const int32_t batLevel = M5.Power.getBatteryLevel();
    const bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
    gState = zd::reduce(gState, zd::TickEvent{nowMs, batLevel, charging});

    // 6. Refresh display at ~20 FPS (50ms frame pacing)
    if ((nowMs - gLastRenderMs) >= 50u) {
        gLastRenderMs = nowMs;
        renderCurrentState();
    }

    delay(15);
}
