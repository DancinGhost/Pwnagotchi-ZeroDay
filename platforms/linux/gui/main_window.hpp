#pragma once

// Qt6 desktop dashboard for pwnagotchi-zeroday. Wraps the shared AppController: the pet
// screen, a live network table, a defensive-alerts feed, recon/monitor controls, and the
// authorization-gated offensive panel (deauth). The transmit gate lives in the pure
// SecSession; this window only exposes it and refuses to arm it without explicit operator
// confirmation + an allow-list entry.

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

#include "../app/controller.hpp"
#include "pet_widget.hpp"

namespace zd {
namespace linux_gui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(linux_app::AppController& app, QWidget* parent = nullptr);

private slots:
    void onTick();
    void onScan();
    void onToggleMonitor();
    void onCycleTheme();
    void onCycleView();
    void onLoadSession();
    void onSaveSession();
    void onAuthToggled(bool on);
    void onAllowSelected();
    void onDeauthSelected();

private:
    void refreshNetworks();
    void refreshStations();
    void refreshAlerts();
    void refreshStatus();
    void refreshChannels();
    void pollAsyncResult();
    std::string selectedBssid() const;

    linux_app::AppController& app_;
    PetWidget* pet_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* secMoodLabel_ = nullptr;
    QLabel* phrase_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* authBanner_ = nullptr;
    QLabel* channelsLabel_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTableWidget* stationsTable_ = nullptr;
    QListWidget* alerts_ = nullptr;
    QCheckBox* authCheck_ = nullptr;
    QPushButton* monitorBtn_ = nullptr;
    QPushButton* deauthBtn_ = nullptr;
    QTimer* timer_ = nullptr;
    std::uint32_t lastTotalAlerts_ = 0;
    std::string lastNetworksSig_;
    std::string lastStationsSig_;
};

}  // namespace linux_gui
}  // namespace zd
