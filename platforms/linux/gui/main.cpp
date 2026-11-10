// Qt6 entry point for the pwnagotchi-zeroday Linux desktop GUI.
//
// Usage: zd-gui [--iface wlan0]
// Monitor mode and deauth require root (CAP_NET_RAW/CAP_NET_ADMIN); recon (active scan)
// works unprivileged via NetworkManager. The offensive panel stays disabled until the
// operator explicitly confirms authorization and allow-lists a target.

#include <string>

#include <QApplication>

#include "../app/controller.hpp"
#include "main_window.hpp"

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);

    std::string iface = "wlan0";
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--iface" && i + 1 < argc) {
            iface = argv[++i];
        }
    }

    zd::linux_app::AppController controller(iface);
    controller.boot();

    zd::linux_gui::MainWindow window(controller);
    window.show();
    return qapp.exec();
}
