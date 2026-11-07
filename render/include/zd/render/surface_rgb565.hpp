#pragma once

// Portable zero-dependency RGB565 software framebuffer & Font0 rasterizer.
//
// Satisfies the `Gfx` concept expected by `zd::render::paintUiFrame` using pure C++17
// with zero external graphics libraries. Ideal for:
//   - Linux `/dev/fb0` or SPI TFT drivers without M5GFX / LovyanGFX
//   - Raspberry Pi / Waveshare 1-bit e-Paper HATs & SSD1306 OLEDs (`toMono1BitPacked`)
//   - Desktop SDL / OpenGL / PPM image export (`toRgb888Buffer`)
//   - Headless host unit tests (`tests/render/test_painter.cpp`)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "zd/theme.hpp"

namespace zd {
namespace render {

// Classic 5x7 GLCD / Font0 printable ASCII (0x20 ' ' .. 0x7E '~'), 5 column bytes per glyph.
inline constexpr std::array<std::array<uint8_t, 5>, 95> kFont5x7 = {{
    {{0x00, 0x00, 0x00, 0x00, 0x00}}, {{0x00, 0x00, 0x5F, 0x00, 0x00}}, {{0x00, 0x07, 0x00, 0x07, 0x00}},
    {{0x14, 0x7F, 0x14, 0x7F, 0x14}}, {{0x24, 0x2A, 0x7F, 0x2A, 0x12}}, {{0x23, 0x13, 0x08, 0x64, 0x62}},
    {{0x36, 0x49, 0x55, 0x22, 0x50}}, {{0x00, 0x05, 0x03, 0x00, 0x00}}, {{0x00, 0x1C, 0x22, 0x41, 0x00}},
    {{0x00, 0x41, 0x22, 0x1C, 0x00}}, {{0x14, 0x08, 0x3E, 0x08, 0x14}}, {{0x08, 0x08, 0x3E, 0x08, 0x08}},
    {{0x00, 0x50, 0x30, 0x00, 0x00}}, {{0x08, 0x08, 0x08, 0x08, 0x08}}, {{0x00, 0x60, 0x60, 0x00, 0x00}},
    {{0x20, 0x10, 0x08, 0x04, 0x02}}, {{0x3E, 0x51, 0x49, 0x45, 0x3E}}, {{0x00, 0x42, 0x7F, 0x40, 0x00}},
    {{0x42, 0x61, 0x51, 0x49, 0x46}}, {{0x21, 0x41, 0x45, 0x4B, 0x31}}, {{0x18, 0x14, 0x12, 0x7F, 0x10}},
    {{0x27, 0x45, 0x45, 0x45, 0x39}}, {{0x3C, 0x4A, 0x49, 0x49, 0x30}}, {{0x01, 0x71, 0x09, 0x05, 0x03}},
    {{0x36, 0x49, 0x49, 0x49, 0x36}}, {{0x06, 0x49, 0x49, 0x29, 0x1E}}, {{0x00, 0x36, 0x36, 0x00, 0x00}},
    {{0x00, 0x56, 0x36, 0x00, 0x00}}, {{0x08, 0x14, 0x22, 0x41, 0x00}}, {{0x14, 0x14, 0x14, 0x14, 0x14}},
    {{0x00, 0x41, 0x22, 0x14, 0x08}}, {{0x02, 0x01, 0x51, 0x09, 0x06}}, {{0x32, 0x49, 0x79, 0x41, 0x3E}},
    {{0x7E, 0x11, 0x11, 0x11, 0x7E}}, {{0x7F, 0x49, 0x49, 0x49, 0x36}}, {{0x3E, 0x41, 0x41, 0x41, 0x22}},
    {{0x7F, 0x41, 0x41, 0x22, 0x1C}}, {{0x7F, 0x49, 0x49, 0x49, 0x41}}, {{0x7F, 0x09, 0x09, 0x09, 0x01}},
    {{0x3E, 0x41, 0x49, 0x49, 0x7A}}, {{0x7F, 0x08, 0x08, 0x08, 0x7F}}, {{0x00, 0x41, 0x7F, 0x41, 0x00}},
    {{0x20, 0x40, 0x41, 0x3F, 0x01}}, {{0x7F, 0x08, 0x14, 0x22, 0x41}}, {{0x7F, 0x40, 0x40, 0x40, 0x40}},
    {{0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {{0x7F, 0x04, 0x08, 0x10, 0x7F}}, {{0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {{0x7F, 0x09, 0x09, 0x09, 0x06}}, {{0x3E, 0x41, 0x51, 0x21, 0x5E}}, {{0x7F, 0x09, 0x19, 0x29, 0x46}},
    {{0x46, 0x49, 0x49, 0x49, 0x31}}, {{0x01, 0x01, 0x7F, 0x01, 0x01}}, {{0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {{0x1F, 0x20, 0x40, 0x20, 0x1F}}, {{0x3F, 0x40, 0x38, 0x40, 0x3F}}, {{0x63, 0x14, 0x08, 0x14, 0x63}},
    {{0x07, 0x08, 0x70, 0x08, 0x07}}, {{0x61, 0x51, 0x49, 0x45, 0x43}}, {{0x00, 0x7F, 0x41, 0x41, 0x00}},
    {{0x02, 0x04, 0x08, 0x10, 0x20}}, {{0x00, 0x41, 0x41, 0x7F, 0x00}}, {{0x04, 0x02, 0x01, 0x02, 0x04}},
    {{0x40, 0x40, 0x40, 0x40, 0x40}}, {{0x00, 0x01, 0x02, 0x04, 0x00}}, {{0x20, 0x54, 0x54, 0x54, 0x78}},
    {{0x7F, 0x48, 0x44, 0x44, 0x38}}, {{0x38, 0x44, 0x44, 0x44, 0x20}}, {{0x38, 0x44, 0x44, 0x48, 0x7F}},
    {{0x38, 0x54, 0x54, 0x54, 0x18}}, {{0x08, 0x7E, 0x09, 0x01, 0x02}}, {{0x0C, 0x52, 0x52, 0x52, 0x3E}},
    {{0x7F, 0x08, 0x04, 0x04, 0x78}}, {{0x00, 0x44, 0x7D, 0x40, 0x00}}, {{0x20, 0x40, 0x44, 0x3D, 0x00}},
    {{0x7F, 0x10, 0x28, 0x44, 0x00}}, {{0x00, 0x41, 0x7F, 0x40, 0x00}}, {{0x7C, 0x04, 0x18, 0x04, 0x78}},
    {{0x7C, 0x08, 0x04, 0x04, 0x78}}, {{0x38, 0x44, 0x44, 0x44, 0x38}}, {{0x7C, 0x14, 0x14, 0x14, 0x08}},
    {{0x08, 0x14, 0x14, 0x18, 0x7C}}, {{0x7C, 0x08, 0x04, 0x04, 0x08}}, {{0x48, 0x54, 0x54, 0x54, 0x20}},
    {{0x04, 0x3F, 0x44, 0x40, 0x20}}, {{0x3C, 0x40, 0x40, 0x20, 0x7C}}, {{0x1C, 0x20, 0x40, 0x20, 0x1C}},
    {{0x3C, 0x40, 0x30, 0x40, 0x3C}}, {{0x44, 0x28, 0x10, 0x28, 0x44}}, {{0x0C, 0x50, 0x50, 0x50, 0x3C}},
    {{0x44, 0x64, 0x54, 0x4C, 0x44}}, {{0x00, 0x08, 0x36, 0x41, 0x00}}, {{0x00, 0x00, 0x7F, 0x00, 0x00}},
    {{0x00, 0x41, 0x36, 0x08, 0x00}}, {{0x08, 0x04, 0x08, 0x10, 0x08}}
}};

class Rgb565Surface {
public:
    Rgb565Surface(int16_t width, int16_t height)
        : width_((std::max)(static_cast<int16_t>(1), width)),
          height_((std::max)(static_cast<int16_t>(1), height)),
          pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0u) {}

    int16_t width() const { return width_; }
    int16_t height() const { return height_; }
    const std::vector<uint16_t>& pixels() const { return pixels_; }
    uint32_t outOfBoundsDrops() const { return clippedPixels_; }

    uint16_t pixelAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return 0u;
        }
        return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(x)];
    }

    void setPixel(int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            ++clippedPixels_;
            return;
        }
        pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                static_cast<std::size_t>(x)] = color;
    }

    void fillScreen(uint16_t color) {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }

    void fillRect(int x, int y, int rw, int rh, uint16_t color) {
        if (rw <= 0 || rh <= 0) {
            return;
        }
        const int x0 = (std::max)(0, x);
        const int y0 = (std::max)(0, y);
        const int x1 = (std::min)(static_cast<int>(width_), x + rw);
        const int y1 = (std::min)(static_cast<int>(height_), y + rh);
        if (x < 0 || y < 0 || x + rw > width_ || y + rh > height_) {
            ++clippedPixels_;
        }
        for (int py = y0; py < y1; ++py) {
            const std::size_t row = static_cast<std::size_t>(py) * static_cast<std::size_t>(width_);
            for (int px = x0; px < x1; ++px) {
                pixels_[row + static_cast<std::size_t>(px)] = color;
            }
        }
    }

    void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
        const int dx = std::abs(x1 - x0);
        const int sx = (x0 < x1) ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = (y0 < y1) ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            setPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
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

    void fillCircle(int cx, int cy, int r, uint16_t color) {
        if (r <= 0) {
            setPixel(cx, cy, color);
            return;
        }
        const int rSq = r * r;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= rSq) {
                    setPixel(cx + dx, cy + dy, color);
                }
            }
        }
    }

    void fillRoundRect(int x, int y, int rw, int rh, int radius, uint16_t color) {
        if (rw <= 0 || rh <= 0) {
            return;
        }
        const int r = (std::max)(0, (std::min)(radius, (std::min)(rw / 2, rh / 2)));
        fillRect(x + r, y, rw - 2 * r, rh, color);
        fillRect(x, y + r, r, rh - 2 * r, color);
        fillRect(x + rw - r, y + r, r, rh - 2 * r, color);
        if (r > 0) {
            const int rSq = r * r;
            for (int dy = 0; dy <= r; ++dy) {
                for (int dx = 0; dx <= r; ++dx) {
                    if (dx * dx + dy * dy <= rSq) {
                        setPixel(x + r - dx, y + r - dy, color);
                        setPixel(x + rw - 1 - r + dx, y + r - dy, color);
                        setPixel(x + r - dx, y + rh - 1 - r + dy, color);
                        setPixel(x + rw - 1 - r + dx, y + rh - 1 - r + dy, color);
                    }
                }
            }
        }
    }

    void fillArc(int cx, int cy, int r0, int r1, float deg0, float deg1, uint16_t color) {
        const int rOuter = (std::max)(r0, r1);
        const int rInner = (std::max)(0, (std::min)(r0, r1));
        if (rOuter <= 0) {
            return;
        }
        const auto normDeg = [](float d) {
            float m = std::fmod(d, 360.0f);
            return (m < 0.0f) ? (m + 360.0f) : m;
        };
        const float a0 = normDeg(deg0);
        const float a1 = normDeg(deg1);
        const bool wraps = a0 > a1;
        const int outerSq = rOuter * rOuter;
        const int innerSq = rInner * rInner;
        constexpr float kRadToDeg = 57.2957795f;

        for (int dy = -rOuter; dy <= rOuter; ++dy) {
            for (int dx = -rOuter; dx <= rOuter; ++dx) {
                const int dSq = dx * dx + dy * dy;
                if (dSq > outerSq || dSq < innerSq) {
                    continue;
                }
                float deg = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * kRadToDeg;
                if (deg < 0.0f) {
                    deg += 360.0f;
                }
                const bool inAngle = wraps ? (deg >= a0 || deg <= a1) : (deg >= a0 && deg <= a1);
                if (inAngle) {
                    setPixel(cx + dx, cy + dy, color);
                }
            }
        }
    }

    void setTextColor(uint16_t fg, uint16_t bg) {
        textFg_ = fg;
        textBg_ = bg;
    }

    void drawString(const char* s, int x, int y) {
        if (s == nullptr) {
            return;
        }
        int cx = x;
        for (std::size_t i = 0; s[i] != '\0'; ++i) {
            const unsigned char ch = static_cast<unsigned char>(s[i]);
            const std::size_t idx = (ch >= 32u && ch <= 126u) ? (ch - 32u) : 31u;  // '?'
            const auto& glyph = kFont5x7[idx];
            for (int col = 0; col < 5; ++col) {
                const uint8_t bits = glyph[static_cast<std::size_t>(col)];
                for (int row = 0; row < 8; ++row) {
                    const bool on = (bits & static_cast<uint8_t>(1u << row)) != 0u;
                    setPixel(cx + col, y + row, on ? textFg_ : textBg_);
                }
            }
            for (int row = 0; row < 8; ++row) {
                setPixel(cx + 5, y + row, textBg_);
            }
            cx += 6;
        }
    }

    // Export 24-bit R,G,B byte stream (width * height * 3 bytes).
    std::vector<uint8_t> toRgb888Buffer() const {
        std::vector<uint8_t> out;
        out.reserve(pixels_.size() * 3u);
        for (uint16_t px : pixels_) {
            const Rgb888 c = fromRgb565(px);
            out.push_back(c.r);
            out.push_back(c.g);
            out.push_back(c.b);
        }
        return out;
    }

    // Export row-major MSB-first 1-bit monochrome bitmap for e-Paper HATs / OLEDs.
    // Every pixel differing from `bgRgb565` is treated as foreground ink (`1` unless `invert`).
    std::vector<uint8_t> toMono1BitPacked(uint16_t bgRgb565, bool invert = false) const {
        const std::size_t stride = (static_cast<std::size_t>(width_) + 7u) / 8u;
        std::vector<uint8_t> out(stride * static_cast<std::size_t>(height_), invert ? 0xFFu : 0x00u);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const bool isInk = (pixelAt(x, y) != bgRgb565);
                const bool bitVal = invert ? !isInk : isInk;
                const std::size_t byteIdx =
                    static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x / 8);
                const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
                if (bitVal) {
                    out[byteIdx] = static_cast<uint8_t>(out[byteIdx] | mask);
                } else {
                    out[byteIdx] = static_cast<uint8_t>(out[byteIdx] & static_cast<uint8_t>(~mask));
                }
            }
        }
        return out;
    }

private:
    int16_t width_;
    int16_t height_;
    std::vector<uint16_t> pixels_;
    uint16_t textFg_ = 0xFFFFu;
    uint16_t textBg_ = 0x0000u;
    uint32_t clippedPixels_ = 0u;
};

}  // namespace render
}  // namespace zd
