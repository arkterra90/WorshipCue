#include "LicenseDialog.h"
#include "../licensing/LicenseManager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
LicenseDialog::LicenseDialog(LicenseManager *license, QWidget *parent) : QDialog(parent) {
    setObjectName("licenseDialog");
    setWindowTitle("License Activation & Devices");
    resize(680, 480);
    auto *layout = new QVBoxLayout(this);
    auto *status = new QLabel(this);
    status->setObjectName("licenseStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *info = new QLabel(
        "Enter your activation code once. WorshipCue remembers this computer without "
        "requesting your computer login password. Editing, libraries, and service creation "
        "are always available. A "
        "license enables live audience and stage output, live media playback, "
        "and MIDI control on up to five installations. Offline authorization "
        "lasts seven days; connect regularly to renew it.",
        this);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *form = new QFormLayout;
    auto *code = new QLineEdit(this);
    code->setObjectName("licenseCode");
    code->setEchoMode(QLineEdit::Password);
    code->setMaxLength(64);
    code->setPlaceholderText("WC-…");
    auto *label = new QLineEdit(QStringLiteral("Presentation computer"), this);
    label->setMaxLength(80);
    form->addRow("License code", code);
    form->addRow("Device name", label);
    layout->addLayout(form);
    auto *actions = new QHBoxLayout;
    auto *activate = new QPushButton("Activate", this);
    activate->setObjectName("activateLicense");
    auto *verify = new QPushButton("Verify / Refresh Devices", this);
    auto *deactivate = new QPushButton("Deactivate This Machine…", this);
    actions->addWidget(activate);
    actions->addWidget(verify);
    actions->addWidget(deactivate);
    layout->addLayout(actions);
    auto *devices = new QTableWidget(0, 3, this);
    devices->setHorizontalHeaderLabels({"Device", "Installation", "Last verified (UTC)"});
    devices->setSelectionBehavior(QAbstractItemView::SelectRows);
    devices->setSelectionMode(QAbstractItemView::SingleSelection);
    devices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    devices->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(devices);
    auto *release = new QPushButton("Deactivate Selected Device…", this);
    layout->addWidget(release);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(activate, &QPushButton::clicked, this, [=] {
        const auto credential = code->text();
        code->clear();
        license->activate(credential, label->text());
    });
    connect(verify, &QPushButton::clicked, license, &LicenseManager::refreshDevices);
    auto confirm = [this] {
        return QMessageBox::question(
                   this, "Deactivate installation?",
                   "This frees an activation for another machine. This machine "
                   "enters editing-only mode when deactivated. A remote device may "
                   "retain offline authorization for up to seven days.",
                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
    };
    connect(deactivate, &QPushButton::clicked, this, [=] {
        if (confirm())
            license->deactivate();
    });
    connect(release, &QPushButton::clicked, this, [=] {
        const int row = devices->currentRow();
        if (row >= 0 && confirm())
            license->releaseDevice(devices->item(row, 0)->data(Qt::UserRole).toString());
    });
    auto refresh = [=] {
        status->setText(license->status());
        const bool idle = !license->busy();
        activate->setEnabled(idle && license->configured());
        verify->setEnabled(idle && !license->licenseId().isEmpty());
        deactivate->setEnabled(idle && !license->licenseId().isEmpty());
        release->setEnabled(idle && !license->licenseId().isEmpty());
        devices->setRowCount(0);
        for (const auto &value : license->devices()) {
            const auto d = value.toObject();
            int row = devices->rowCount();
            devices->insertRow(row);
            auto *name = new QTableWidgetItem(
                d["label"].toString() +
                (d["id"].toString() == license->installationId() ? " (this machine)" : ""));
            name->setData(Qt::UserRole, d["id"].toString());
            devices->setItem(row, 0, name);
            devices->setItem(row, 1, new QTableWidgetItem(d["id"].toString().left(12)));
            devices->setItem(
                row, 2,
                new QTableWidgetItem(
                    QDateTime::fromSecsSinceEpoch(d["verifiedAt"].toVariant().toLongLong(), Qt::UTC)
                        .toString(Qt::ISODate)));
        }
    };
    connect(license, &LicenseManager::changed, this, refresh);
    refresh();
}
