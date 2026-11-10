#include "main_window.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
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
    status_ = new QLabel("booting…", central);
    status_->setStyleSheet("font-weight:bold; font-size:14px;");
    phrase_ = new QLabel("", central);
    phrase_->setWordWrap(true);
    engineLabel_ = new QLabel("", central);
    engineLabel_->setStyleSheet("color:#888;");
    statusBox->addWidget(status_);
    statusBox->addWidget(phrase_);
    statusBox->addWidget(engineLabel_);

    auto* btnRow = new QHBoxLayout();
    auto* scanBtn = new QPushButton("Scan", central);
    monitorBtn_ = new QPushButton("Start Monitor", central);
    auto* themeBtn = new QPushButton("Theme", central);
    auto* viewBtn = new QPushButton("View", central);
    btnRow->addWidget(scanBtn);
    btnRow->addWidget(monitorBtn_);
    btnRow->addWidget(themeBtn);
    btnRow->addWidget(viewBtn);
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
    channelsLabel_->setStyleSheet("color:#888; font-family:monospace;");
    statusBox->addWidget(channelsLabel_);
    statusBox->addStretch(1);

    top->addLayout(statusBox, 2);
    root->addLayout(top, 2);

    // --- Bottom: network table + alerts ---
    auto* split = new QSplitter(Qt::Horizontal, central);

    table_ = new QTableWidget(0, 7, split);
    table_->setHorizontalHeaderLabels({"BSSID", "SSID", "CH", "dBm", "ENC", "Clients", "Capture"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    split->addWidget(table_);

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
    connect(authCheck_, &QCheckBox::toggled, this, &MainWindow::onAuthToggled);
    connect(allowBtn, &QPushButton::clicked, this, &MainWindow::onAllowSelected);
    connect(deauthBtn_, &QPushButton::clicked, this, &MainWindow::onDeauthSelected);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);
    timer_->start(100);

    app_.requestScan();
    refreshStatus();
}

void MainWindow::onTick() {
    app_.step();
    pollAsyncResult();
    pet_->setFrame(app_.petFrame(ScreenDimensions{240, 135}));
    refreshNetworks();
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
        set(5, QString::number(t.clientCount));
        set(6, t.handshake ? QString("HANDSHAKE") : (t.pmkid ? QString("PMKID") : QString("-")));
        if (t.bssid == selBssid) {
            selRow = i;
        }
    }
    if (selRow >= 0) {
        table_->selectRow(selRow);
    }
}

void MainWindow::refreshAlerts() {
    const auto& alerts = app_.secState().alerts;
    if (alerts.size() == lastAlertCount_) {
        return;
    }
    for (std::size_t i = lastAlertCount_; i < alerts.size(); ++i) {
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
    alerts_->scrollToBottom();
    lastAlertCount_ = alerts.size();
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
