// Native Desktop / Linux SBC Shell for Pwnagotchi-ZeroDay (Linux / macOS / Windows / RPi).
//
// Runs the exact portable functional core (`core/include/zd/`) and software pixel painter
// (`render/include/zd/render/`) on any POSIX/Windows host. Ideal for previewing themes,
// testing multi-device screen sizes, exporting pixel-accurate `.ppm` screenshots, and
// serving as a reference template for Linux SBC / e-Paper / framebuffer ports.
//
// Usage:
//   ./build/zd_sim                         Interactive session (type "help")
//   ./build/zd_sim --demo                  Print every theme once and exit
//   ./build/zd_sim --size <key>            micro | compact | plus | epaper | wide
//   ./build/zd_sim --theme <key>           Start on a specific theme (e.g. neon, volt, operator)
//   ./build/zd_sim --ppm <file.ppm>        Render pixel-accurate RGB frame to a PPM image & exit
//   ./build/zd_sim --no-color              Plain monochrome ASCII output

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "zd/fp.hpp"
#include "zd/hal.hpp"
#include "zd/render/painter.hpp"
#include "zd/render/surface_rgb565.hpp"
#include "zd/theme.hpp"
#include "zd/ui_model.hpp"

namespace {

struct Options {
    bool demo = false;
    bool color = true;
    zd::ScreenDimensions dims{240, 135};
    std::string theme;
    std::string ppmPath;
};

uint32_t nowMs() {
    static const auto t0 = std::chrono::steady_clock::now();
    const auto dt = std::chrono::steady_clock::now() - t0;
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count());
}

class SimulatedRadioScanner final : public zd::hal::IRadioScanner {
public:
    explicit SimulatedRadioScanner(uint32_t seed = 0xC0FFEEu) : seed_(seed) {}

    bool startAsyncScan() override {
        seed_ = fp::nextSeed(seed_);
        return true;
    }

    zd::hal::ScanPollResult pollScan() override {
        static constexpr std::array<const char*, 12> kNames = {
            "CoffeeShop_Guest", "HomeNet_2G", "PrinterSetup", "Lab-Bench", "Mesh_Node_A",
            "Mesh_Node_B", "Neighbor_WiFi", "Library_Free", "IoT_Bridge", "GarageDoor",
            "Studio_AP", "Hotspot_42"
        };
        std::vector<zd::AccessPoint> out;
        uint32_t s = seed_;
        const std::size_t count = 3u + (s % 9u);
        for (std::size_t i = 0; i < count; ++i) {
            s = fp::nextSeed(s);
            out.push_back(zd::AccessPoint{
                kNames[s % kNames.size()],
                -40 - static_cast<int32_t>(s % 50u),
                1 + static_cast<int32_t>((s >> 8) % 13u),
                ((s >> 4) % 4u) != 0u
            });
        }
        seed_ = s;
        return {zd::hal::ScanPollState::Completed, std::move(out)};
    }

private:
    uint32_t seed_;
};

std::string ansiColor(bool background, zd::Rgb888 c) {
    return "\x1b[" + std::string(background ? "48" : "38") + ";2;" + std::to_string(c.r) + ";" +
           std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
}

void printTerminalFrame(const zd::UiFrame& frame, bool color) {
    const std::string ascii = zd::renderAsciiPreview(frame);
    const zd::ThemeSpec& spec = zd::themeSpec(frame.theme);

    std::cout << "theme: " << spec.name << " [" << spec.key << "] (" << frame.dims.width << "x"
              << frame.dims.height << ") - " << spec.blurb << "\n";
    if (!color) {
        std::cout << ascii;
        return;
    }
    const std::string style = ansiColor(true, spec.palette.bg) + ansiColor(false, spec.palette.fg);
    std::size_t start = 0;
    while (start < ascii.size()) {
        const std::size_t end = ascii.find('\n', start);
        const std::string line = ascii.substr(start, end - start);
        std::cout << style << line << "\x1b[0m\n";
        if (end == std::string::npos) {
            break;
        }
        start = end + 1u;
    }
}

bool writePpmScreenshot(const std::string& path, const zd::UiFrame& frame) {
    zd::render::Rgb565Surface surf(frame.dims.width, frame.dims.height);
    zd::render::paintUiFrame(surf, frame);
    const std::vector<uint8_t> rgb = surf.toRgb888Buffer();

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << surf.width() << " " << surf.height() << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return static_cast<bool>(out);
}

void runInstantScan(zd::hal::PlatformSession& session, SimulatedRadioScanner& radio, uint32_t t) {
    session.dispatch(zd::ButtonBPressedEvent{t});
    session.stepRadio(t, radio);
    session.stepRadio(t + 1u, radio);
}

bool parseOptions(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--demo") {
            opt.demo = true;
        } else if (a == "--compact") {
            opt.dims = {160, 80};
        } else if (a == "--no-color") {
            opt.color = false;
        } else if (a == "--theme" && i + 1 < argc) {
            opt.theme = argv[++i];
        } else if (a == "--ppm" && i + 1 < argc) {
            opt.ppmPath = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            const std::string key = argv[++i];
            if (const auto prof = zd::screenProfileFromKey(key)) {
                opt.dims = prof->dims;
            } else {
                std::cerr << "Unknown screen profile '" << key
                          << "'. Choose: micro | compact | plus | epaper | wide\n";
                return false;
            }
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: zd_sim [--demo] [--theme <key>] "
                         "[--size <micro|compact|plus|epaper|wide>] [--ppm <out.ppm>] [--no-color]\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseOptions(argc, argv, opt)) {
        return 0;
    }

    zd::hal::MemoryThemeStore store(opt.theme);
    zd::hal::FixedPowerSensor power(100, false);
    SimulatedRadioScanner radio(0xC0FFEEu);
    zd::hal::PlatformSession session;

    if (!opt.theme.empty() && !zd::themeFromKey(opt.theme).has_value()) {
        std::cerr << "Unknown theme '" << opt.theme << "'.\n"
                  << zd::hal::formatThemeTable(zd::ThemeId::PaperInk);
        return 1;
    }

    session.boot(0xC0FFEEu, nowMs(), &store);
    runInstantScan(session, radio, nowMs());

    if (!opt.ppmPath.empty()) {
        if (!writePpmScreenshot(opt.ppmPath, session.buildFrame(opt.dims))) {
            std::cerr << "Failed to write PPM image to " << opt.ppmPath << "\n";
            return 1;
        }
        std::cout << "Wrote " << opt.dims.width << "x" << opt.dims.height << " screenshot ("
                  << zd::themeSpec(session.state().theme).name << ") to " << opt.ppmPath << "\n";
        return 0;
    }

    if (opt.demo) {
        for (std::size_t i = 0; i < zd::kThemeCount; ++i) {
            printTerminalFrame(session.buildFrame(opt.dims), opt.color);
            session.dispatch(zd::CycleThemeEvent{nowMs(), +1}, &store);
        }
        return 0;
    }

    std::cout << "Pwnagotchi-ZeroDay desktop simulator. Type 'help' or 'themes'.\n";
    printTerminalFrame(session.buildFrame(opt.dims), opt.color);

    std::string line;
    while (std::cout << "zd> " << std::flush, std::getline(std::cin, line)) {
        const uint32_t t = nowMs();
        if (line.rfind("size ", 0) == 0) {
            const std::string key(zd::hal::trimAscii(std::string_view(line).substr(5)));
            if (const auto prof = zd::screenProfileFromKey(key)) {
                opt.dims = prof->dims;
                printTerminalFrame(session.buildFrame(opt.dims), opt.color);
            } else {
                std::cout << "Profiles: micro | compact | plus | epaper | wide\n";
            }
            continue;
        }

        const zd::hal::CliOutcome res =
            zd::hal::interpretCliLine(line, session.state(), opt.dims, t);
        if (res.requestQuit) {
            break;
        }
        if (!res.output.empty()) {
            std::cout << res.output;
            continue;
        }
        if (res.event.has_value()) {
            session.dispatch(*res.event, &store);
            if (session.state().manualScanRequested) {
                session.stepRadio(t, radio);
                session.stepRadio(t + 1u, radio);
            }
        }
        session.stepTick(nowMs(), power);
        printTerminalFrame(session.buildFrame(opt.dims), opt.color);
    }
    return 0;
}
