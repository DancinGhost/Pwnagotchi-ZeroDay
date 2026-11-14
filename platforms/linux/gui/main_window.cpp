#include "main_window.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "zd/theme.hpp"
#include "zd/ui_model.hpp"

namespace zd {
namespace linux_gui {

MainWindow::MainWindow(linux_app::AppController& app, QWidget* parent)
    : QMainWindow(parent), app_(app) {
    setWindowTitle("pwnagotchi-zeroday — Linux");
    resize(1040, 640);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- Top strip: pet screen + status + controls ---
    auto* top = new QHBoxLayout();
    pet_ = new PetWidget(central);
    top->addWidget(pet_, 3);

    auto* statusBox = new QVBoxLayout();
    auto* statusHeader = new QHBoxLayout();
    status_ = new QLabel("booting…", central);
    status_->setStyleSheet("font-weight:bold; font-size:14px;");
    secMoodLabel_ = new QLabel("SEC: CALM", central);
    secMoodLabel_->setStyleSheet(
        "font-weight:bold; padding:2px 6px; border-radius:4px; background:#145a32; color:#fff;");
    statusHeader->addWidget(status_, 1);
    statusHeader->addWidget(secMoodLabel_, 0);
    phrase_ = new QLabel("", central);
    phrase_->setWordWrap(true);
    engineLabel_ = new QLabel("", central);
    statusBox->addLayout(statusHeader);
    statusBox->addWidget(phrase_);
    statusBox->addWidget(engineLabel_);

    auto* btnRow = new QHBoxLayout();
    auto* scanBtn = new QPushButton("Scan", central);
    monitorBtn_ = new QPushButton("Start Monitor", central);
    auto* themeBtn = new QPushButton("Theme", central);
    auto* viewBtn = new QPushButton("View", central);
    auto* loadBtn = new QPushButton("Load Session…", central);
    auto* saveBtn = new QPushButton("Save Session…", central);
    btnRow->addWidget(scanBtn);
    btnRow->addWidget(monitorBtn_);
    btnRow->addWidget(themeBtn);
    btnRow->addWidget(viewBtn);
    btnRow->addWidget(loadBtn);
    btnRow->addWidget(saveBtn);
    statusBox->addLayout(btnRow);

    // --- Authorization / offensive panel ---
    auto* authGroup = new QGroupBox("Offensive controls (authorized use only)", central);
    auto* authLayout = new QVBoxLayout(authGroup);
    authBanner_ = new QLabel(central);
    authBanner_->setWordWrap(true);
    authCheck_ = new QCheckBox("I am authorized to transmit against the allow-listed targets",
                               central);
    auto* allowBtn = new QPushButton("Allow selected BSSID", central);
    deauthBtn_ = new QPushButton("Deauth selected…", central);
    deauthBtn_->setStyleSheet("color:#b00; font-weight:bold;");
    authLayout->addWidget(authBanner_);
    authLayout->addWidget(authCheck_);
    auto* authBtnRow = new QHBoxLayout();
    authBtnRow->addWidget(allowBtn);
    authBtnRow->addWidget(deauthBtn_);
    authLayout->addLayout(authBtnRow);
    statusBox->addWidget(authGroup);

    channelsLabel_ = new QLabel(central);
    channelsLabel_->setWordWrap(true);
    channelsLabel_->setStyleSheet("font-family:monospace;");
    statusBox->addWidget(channelsLabel_);
    statusBox->addStretch(1);

    top->addLayout(statusBox, 2);
    root->addLayout(top, 2);

    // --- Bottom: tabbed network + station tables & defensive alerts ---
    auto* split = new QSplitter(Qt::Horizontal, central);

    tabs_ = new QTabWidget(split);
    table_ = new QTableWidget(0, 8, tabs_);
    table_->setHorizontalHeaderLabels(
        {"BSSID", "SSID", "CH", "dBm", "ENC", "WPS", "Clients", "Capture"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs_->addTab(table_, "Networks (0)");

    stationsTable_ = new QTableWidget(0, 5, tabs_);
    stationsTable_->setHorizontalHeaderLabels(
        {"Station MAC", "Associated BSSID", "Probes", "Risk", "Probed SSIDs"});
    stationsTable_->horizontalHeader()->setStretchLastSection(true);
    stationsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    stationsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    stationsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs_->addTab(stationsTable_, "Stations / Probes (0)");

    split->addWidget(tabs_);

    auto* alertBox = new QWidget(split);
    auto* alertLayout = new QVBoxLayout(alertBox);
    alertLayout->addWidget(new QLabel("Defensive alerts", alertBox));
    alerts_ = new QListWidget(alertBox);
    alertLayout->addWidget(alerts_);
    split->addWidget(alertBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 3);

    setCentralWidget(central);
    statusBar()->showMessage(QString("Audit log: %1").arg(QString::fromStdString(app_.auditPath())));

    // --- Wiring ---
    connect(scanBtn, &QPushButton::clicked, this, &MainWindow::onScan);
    connect(monitorBtn_, &QPushButton::clicked, this, &MainWindow::onToggleMonitor);
    connect(themeBtn, &QPushButton::clicked, this, &MainWindow::onCycleTheme);
    connect(viewBtn, &QPushButton::clicked, this, &MainWindow::onCycleView);
    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::onLoadSession);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveSession);
    connect(authCheck_, &QCheckBox::toggled, this, &MainWindow::onAuthToggled);
    connect(allowBtn, &QPushButton::clicked, this, &MainWindow::onAllowSelected);
    connect(deauthBtn_, &QPushButton::clicked, this, &MainWindow::onDeauthSelected);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);
    timer_->start(100);

    if (app_.secState().targets.empty()) {
        app_.requestScan();
    }
    refreshStatus();
}

void MainWindow::onTick() {
    app_.step();
    pollAsyncResult();
    pet_->setFrame(app_.petFrame(ScreenDimensions{240, 135}));
    pet_->setSecMood(app_.secMood());
    refreshNetworks();
    refreshStations();
    refreshAlerts();
    refreshChannels();
    refreshStatus();
}

void MainWindow::pollAsyncResult() {
    // Monitor start/stop and deauth all run on a worker thread so this tick never blocks;
    // their outcome (success/failure + message) surfaces here once it's ready. Status-bar
    // only (not a modal): the user has usually moved on by the time a multi-second
    // airmon-ng/aireplay-ng call finishes, so a surprise dialog would be worse UX than this.
    if (auto r = app_.takeLastAsyncResult()) {
        statusBar()->showMessage(QString::fromStdString(r->message), 6000);
    }
}

void MainWindow::refreshStatus() {
    const AppState& ps = app_.petState();
    status_->setText(QString("%1  ·  mood %2  ·  battery %3%%")
                         .arg(themeSpec(ps.theme).name)
                         .arg(faceForMood(ps.mood).moodLabel)
                         .arg(ps.batteryPercent));
    const sec::SecMood sm = app_.secMood();
    secMoodLabel_->setText(QString("SEC: %1").arg(sec::secMoodLabel(sm)));
    if (sm == sec::SecMood::Alarmed) {
        secMoodLabel_->setStyleSheet(
            "font-weight:bold; padding:2px 6px; border-radius:4px; background:#b00; color:#fff;");
    } else if (sm == sec::SecMood::Watchful) {
        secMoodLabel_->setStyleSheet(
            "font-weight:bold; padding:2px 6px; border-radius:4px; background:#9a6000; color:#fff;");
    } else {
        secMoodLabel_->setStyleSheet(
            "font-weight:bold; padding:2px 6px; border-radius:4px; background:#145a32; color:#fff;");
    }
    phrase_->setText(QString::fromStdString(ps.phrase));
    engineLabel_->setText(QString("Interface %1 · %2")
                              .arg(QString::fromStdString(app_.interfaceName()))
                              .arg(app_.monitoring()
                                       ? QString::fromStdString(app_.engineLabel())
                                       : QString("recon (active scan only)")));
    const bool busy = app_.asyncBusy();
    const auto pending = app_.pendingActionKind();
    QString monitorText = app_.monitoring() ? "Stop Monitor" : "Start Monitor";
    if (pending == linux_app::AsyncActionKind::StartMonitor) {
        monitorText = "Starting…";
    } else if (pending == linux_app::AsyncActionKind::StopMonitor) {
        monitorText = "Stopping…";
    }
    monitorBtn_->setText(monitorText);
    monitorBtn_->setEnabled(!busy);
    deauthBtn_->setEnabled(!busy);

    const auto& auth = app_.authorization();
    if (auth.operatorConfirmed) {
        authBanner_->setText(QString("ARMED — allow-list holds %1 BSSID(s). Transmit permitted "
                                     "only against those.")
                                 .arg(auth.allowedBssids.size()));
        authBanner_->setStyleSheet("color:#0a0;");
    } else {
        authBanner_->setText("Safe: transmit disabled. Confirm authorization AND allow-list a "
                             "BSSID you own/are authorized to test to enable deauth.");
        authBanner_->setStyleSheet("color:#b00;");
    }
}

void MainWindow::refreshNetworks() {
    const auto targets = sec::targetsByStrength(app_.secState());
    std::string sig = std::to_string(targets.size()) + ":";
    for (const auto& t : targets) {
        sig += t.bssid + "|" + t.ssid + "|" + std::to_string(t.channel) + "|" +
               std::to_string(t.rssi) + "|" + std::to_string(static_cast<int>(t.enc)) + "|" +
               (t.wps ? "1" : "0") + "|" + std::to_string(t.clientCount) + "|" +
               (t.handshake ? "H" : (t.pmkid ? "P" : "-")) + ";";
    }
    if (sig == lastNetworksSig_) {
        return;
    }
    lastNetworksSig_ = std::move(sig);
    tabs_->setTabText(0, QString("Networks (%1)").arg(targets.size()));
    // Preserve selection by BSSID across refreshes.
    const std::string selBssid = selectedBssid();
    table_->setRowCount(static_cast<int>(targets.size()));
    int selRow = -1;
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        const auto& t = targets[i];
        auto set = [&](int col, const QString& v) {
            table_->setItem(i, col, new QTableWidgetItem(v));
        };
        set(0, QString::fromStdString(t.bssid));
        set(1, t.ssid.empty() ? QString("<hidden>") : QString::fromStdString(t.ssid));
        set(2, QString::number(t.channel));
        set(3, QString::number(t.rssi));
        set(4, QString(sec::encryptionLabel(t.enc)));
        auto* wpsItem = new QTableWidgetItem(t.wps ? QString("WPS*") : QString("-"));
        if (t.wps) {
            wpsItem->setForeground(QColor(210, 120, 0));
        }
        table_->setItem(i, 5, wpsItem);
        set(6, QString::number(t.clientCount));
        set(7, t.handshake ? QString("HANDSHAKE") : (t.pmkid ? QString("PMKID") : QString("-")));
        if (t.bssid == selBssid) {
            selRow = i;
        }
    }
    if (selRow >= 0) {
        table_->selectRow(selRow);
    }
}

void MainWindow::refreshStations() {
    const auto stations = sec::stationsByActivity(app_.secState());
    std::string sig = std::to_string(stations.size()) + ":";
    for (const auto& st : stations) {
        sig += st.mac + "|" + st.lastBssid + "|" + std::to_string(st.probeCount) + "|" +
               std::to_string(st.probedSsids.size()) + ";";
    }
    if (sig == lastStationsSig_) {
        return;
    }
    lastStationsSig_ = std::move(sig);
    tabs_->setTabText(1, QString("Stations / Probes (%1)").arg(stations.size()));
    stationsTable_->setRowCount(static_cast<int>(stations.size()));
    for (int i = 0; i < static_cast<int>(stations.size()); ++i) {
        const auto& st = stations[i];
        const bool weak = sec::hasWeakOrDefaultProbe(st);
        QString probedList;
        for (std::size_t k = 0; k < st.probedSsids.size(); ++k) {
            if (k != 0u) {
                probedList += ", ";
            }
            probedList += QString::fromStdString(st.probedSsids[k]);
        }
        if (probedList.isEmpty()) {
            probedList = "<wildcard>";
        }
        stationsTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(st.mac)));
        stationsTable_->setItem(
            i, 1,
            new QTableWidgetItem(st.lastBssid.empty() ? QString("-")
                                                      : QString::fromStdString(st.lastBssid)));
        stationsTable_->setItem(i, 2, new QTableWidgetItem(QString::number(st.probeCount)));
        auto* riskItem = new QTableWidgetItem(weak ? QString("WEAK / DEFAULT") : QString("-"));
        if (weak) {
            riskItem->setForeground(QColor(210, 120, 0));
        }
        stationsTable_->setItem(i, 3, riskItem);
        stationsTable_->setItem(i, 4, new QTableWidgetItem(probedList));
    }
}

void MainWindow::refreshAlerts() {
    const auto& sec = app_.secState();
    const auto& alerts = sec.alerts;
    if (sec.totalAlertsEmitted <= lastTotalAlerts_ || alerts.empty()) {
        return;
    }
    const std::uint32_t unseen = sec.totalAlertsEmitted - lastTotalAlerts_;
    const std::size_t count =
        (unseen < alerts.size()) ? static_cast<std::size_t>(unseen) : alerts.size();
    const std::size_t start = alerts.size() - count;
    for (std::size_t i = start; i < alerts.size(); ++i) {
        const auto& al = alerts[i];
        auto* item = new QListWidgetItem(
            QString("[%1] %2  %3  %4")
                .arg(al.nowMs)
                .arg(sec::alertKindLabel(al.kind))
                .arg(QString::fromStdString(al.bssid))
                .arg(QString::fromStdString(al.message)));
        if (al.severity == sec::Severity::Critical) {
            item->setForeground(Qt::red);
        } else if (al.severity == sec::Severity::Warn) {
            item->setForeground(QColor(200, 140, 0));
        }
        alerts_->addItem(item);
    }
    while (alerts_->count() > static_cast<int>(sec::kMaxAlerts)) {
        delete alerts_->takeItem(0);
    }
    alerts_->scrollToBottom();
    lastTotalAlerts_ = sec.totalAlertsEmitted;
}

void MainWindow::refreshChannels() {
    // Channel distribution across both bands: the Linux backend sees real 5GHz channel
    // numbers (unlike the portable 2.4GHz-only Channel Spectrum pet-UI page).
    const auto dist = sec::channelDistribution(app_.secState());
    QString band24;
    QString band5;
    for (const auto& c : dist) {
        const QString part =
            QString("ch%1×%2 ").arg(c.channel).arg(c.count);
        if (c.band == sec::Band::Band24GHz) {
            band24 += part;
        } else if (c.band == sec::Band::Band5GHz) {
            band5 += part;
        }
    }
    channelsLabel_->setText(QString("2.4GHz: %1\n5GHz: %2")
                                .arg(band24.isEmpty() ? QString("-") : band24)
                                .arg(band5.isEmpty() ? QString("-") : band5));
}

std::string MainWindow::selectedBssid() const {
    const int row = table_->currentRow();
    if (row < 0 || table_->item(row, 0) == nullptr) {
        return {};
    }
    return table_->item(row, 0)->text().toStdString();
}

void MainWindow::onScan() { app_.requestScan(); }

void MainWindow::onToggleMonitor() {
    const auto r = app_.monitoring() ? app_.stopMonitor() : app_.startMonitor();
    statusBar()->showMessage(QString::fromStdString(r.message), 5000);
    if (!r.ok) {
        QMessageBox::warning(this, "Monitor mode", QString::fromStdString(r.message));
    }
}

void MainWindow::onCycleTheme() { app_.dispatchPet(CycleThemeEvent{linux_app::nowMs(), +1}); }
void MainWindow::onCycleView() { app_.dispatchPet(CyclePageEvent{linux_app::nowMs(), +1}); }

void MainWindow::onLoadSession() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Recorded Session", QString(), "ZeroDay Session (*.zds *.log *.txt);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    alerts_->clear();
    lastTotalAlerts_ = 0;
    const auto r = app_.loadSession(path.toStdString());
    statusBar()->showMessage(QString::fromStdString(r.message), 6000);
    if (!r.ok) {
        QMessageBox::warning(this, "Load Session", QString::fromStdString(r.message));
    }
    onTick();
}

void MainWindow::onSaveSession() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Session", "session.zds", "ZeroDay Session (*.zds *.log *.txt);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    const auto r = app_.saveSession(path.toStdString());
    statusBar()->showMessage(QString::fromStdString(r.message), 6000);
    if (!r.ok) {
        QMessageBox::warning(this, "Save Session", QString::fromStdString(r.message));
    }
}

void MainWindow::onAuthToggled(bool on) {
    app_.confirmAuthorization(on);
    refreshStatus();
}

void MainWindow::onAllowSelected() {
    const std::string bssid = selectedBssid();
    if (bssid.empty()) {
        QMessageBox::information(this, "Allow-list", "Select a network row first.");
        return;
    }
    app_.allowBssid(bssid);
    statusBar()->showMessage(QString("Allow-listed %1").arg(QString::fromStdString(bssid)), 4000);
    refreshStatus();
}

void MainWindow::onDeauthSelected() {
    const std::string bssid = selectedBssid();
    if (bssid.empty()) {
        QMessageBox::information(this, "Deauth", "Select a network row first.");
        return;
    }
    // Pre-flight the gate so the operator sees exactly why if it would be refused.
    const sec::AuthDecision decision = sec::authorize(app_.authorization(), bssid);
    if (decision != sec::AuthDecision::Allowed) {
        QMessageBox::critical(
            this, "Transmit blocked",
            QString("Deauth against %1 is blocked:\n\n%2\n\nConfirm authorization and allow-list "
                    "this BSSID first.")
                .arg(QString::fromStdString(bssid))
                .arg(sec::authDecisionLabel(decision)));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Deauthentication — authorized target");
    auto* form = new QFormLayout(&dlg);
    auto* warn = new QLabel(
        "This transmits deauthentication frames. Use only on networks you own or are explicitly "
        "authorized to test. This action is logged.",
        &dlg);
    warn->setWordWrap(true);
    warn->setStyleSheet("color:#b00;");
    form->addRow(warn);
    auto* bssidEdit = new QLineEdit(QString::fromStdString(bssid), &dlg);
    bssidEdit->setReadOnly(true);
    auto* clientEdit = new QLineEdit(&dlg);
    clientEdit->setPlaceholderText("optional client MAC (blank = broadcast)");
    auto* countSpin = new QSpinBox(&dlg);
    countSpin->setRange(1, 1000);
    countSpin->setValue(16);
    form->addRow("Target BSSID", bssidEdit);
    form->addRow("Client", clientEdit);
    form->addRow("Frame count", countSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Transmit");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const std::string client = clientEdit->text().toStdString();
    if (!client.empty() && !sec::isValidMac(client)) {
        QMessageBox::warning(this, "Deauth",
                             QString("'%1' doesn't look like a MAC address.")
                                 .arg(QString::fromStdString(client)));
        return;
    }
    if (!app_.monitoring()) {
        QMessageBox::information(
            this, "Deauth",
            "Start monitor mode first (it has to come up before a target can be hit).");
        return;
    }
    const auto r = app_.requestDeauth(bssid, client, static_cast<uint32_t>(countSpin->value()));
    statusBar()->showMessage(QString::fromStdString(r.message), 6000);
    if (!r.ok) {
        QMessageBox::warning(this, "Deauth", QString::fromStdString(r.message));
        return;
    }
    // r.message here is just the immediate "transmitting..." ack - aireplay-ng runs on a
    // worker thread and can take a while for a large frame count. The final result (and
    // the audit-log reminder) surfaces via the status bar once pollAsyncResult() collects
    // it, same as every other background action.
}

}  // namespace linux_gui
}  // namespace zd
