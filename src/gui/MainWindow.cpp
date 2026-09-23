#include "gui/MainWindow.h"

#include "device/DeviceDiscovery.h"
#include "device/HhkbStudioDevice.h"
#include "device/HidrawTransport.h"
#include "gui/KeyAssignmentDialog.h"
#include "gui/KeyboardWidget.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ProfileSerializer.h"

#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSizePolicy>
#include <QStyle>
#include <QTabBar>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct MainWindow::ScanResult {
    enum class Status {
        Connected,
        NotFound,
        PermissionDenied,
        Failed,
    };

    Status status{Status::NotFound};
    QString devicePath;
    QString productName;
    QString modelName;
    QString keyboardLayout;
    QString firmwareVersion;
    QString errorMessage;
    std::uint16_t currentProfile{};
    std::vector<std::uint8_t> profileBytes;
};

namespace {

QString text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

}  // namespace

MainWindow::MainWindow(const bool demoMode, QWidget* parent)
    : QMainWindow(parent)
{
    buildInterface();
    if (demoMode) {
        keymap_.load(hhkbs::keymap::KeyboardLayout::demoProfile());
        profileLoaded_ = true;
        connectionStatus_->setText(QStringLiteral("Demo profile"));
        connectionStatus_->setProperty("state", QStringLiteral("idle"));
        showEditor(QStringLiteral("Offline US-layout demo · changes are kept in memory"));
        updateActions();
    } else {
        beginDeviceScan();
    }
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("HHKBS"));
    setMinimumSize(940, 620);
    resize(1180, 760);

    auto* centralWidget = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(centralWidget);
    pageLayout->setContentsMargins(32, 28, 32, 28);
    pageLayout->setSpacing(18);

    auto* headerLayout = new QHBoxLayout;
    auto* titleLayout = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("HHKBS"));
    title->setObjectName(QStringLiteral("title"));
    auto* subtitle = new QLabel(QStringLiteral("HHKB Studio Keymap Editor for Linux"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    connectionStatus_ = new QLabel(QStringLiteral("Searching…"));
    connectionStatus_->setObjectName(QStringLiteral("connectionStatus"));
    connectionStatus_->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(connectionStatus_);
    pageLayout->addLayout(headerLayout);

    auto* layerRow = new QHBoxLayout;
    layerTabs_ = new QTabBar;
    layerTabs_->setObjectName(QStringLiteral("layerTabs"));
    layerTabs_->addTab(QStringLiteral("Base"));
    layerTabs_->addTab(QStringLiteral("Fn1"));
    layerTabs_->addTab(QStringLiteral("Fn2"));
    layerTabs_->addTab(QStringLiteral("Fn3"));
    layerTabs_->setExpanding(false);
    layerTabs_->setEnabled(false);
    layerRow->addWidget(layerTabs_);
    layerRow->addStretch();
    profileSummary_ = new QLabel;
    profileSummary_->setObjectName(QStringLiteral("profileSummary"));
    profileSummary_->setVisible(false);
    layerRow->addWidget(profileSummary_);
    pageLayout->addLayout(layerRow);

    workspace_ = new QFrame;
    workspace_->setObjectName(QStringLiteral("workspace"));
    workspace_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* workspaceLayout = new QVBoxLayout(workspace_);
    workspaceLayout->setContentsMargins(16, 16, 16, 16);
    workspaceLayout->setAlignment(Qt::AlignCenter);

    workspaceTitle_ = new QLabel(QStringLiteral("Looking for an HHKB Studio"));
    workspaceTitle_->setObjectName(QStringLiteral("placeholderTitle"));
    workspaceTitle_->setAlignment(Qt::AlignCenter);
    workspaceBody_ = new QLabel(QStringLiteral("Checking available HID interfaces…"));
    workspaceBody_->setObjectName(QStringLiteral("placeholderBody"));
    workspaceBody_->setAlignment(Qt::AlignCenter);
    workspaceBody_->setWordWrap(true);
    workspaceBody_->setMaximumWidth(680);
    keyboardWidget_ = new KeyboardWidget;
    keyboardWidget_->setVisible(false);

    workspaceLayout->addWidget(workspaceTitle_);
    workspaceLayout->addWidget(workspaceBody_);
    workspaceLayout->addWidget(keyboardWidget_, 1);
    pageLayout->addWidget(workspace_, 1);

    auto* actionLayout = new QHBoxLayout;
    refreshButton_ = new QPushButton(QStringLiteral("Read from keyboard"));
    importButton_ = new QPushButton(QStringLiteral("Import"));
    exportButton_ = new QPushButton(QStringLiteral("Export"));
    defaultsButton_ = new QPushButton(QStringLiteral("Restore defaults"));
    discardButton_ = new QPushButton(QStringLiteral("Discard changes"));
    auto* applyButton = new QPushButton(QStringLiteral("Apply to keyboard"));
    applyButton->setObjectName(QStringLiteral("primaryButton"));
    applyButton->setEnabled(false);

    connect(refreshButton_, &QPushButton::clicked, this, [this] {
        if (confirmDiscardChanges()) {
            beginDeviceScan();
        }
    });
    connect(importButton_, &QPushButton::clicked, this, [this] {
        importProfile();
    });
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        static_cast<void>(exportProfile());
    });
    connect(defaultsButton_, &QPushButton::clicked, this, [this] {
        restoreFactoryDefaults();
    });
    connect(discardButton_, &QPushButton::clicked, this, [this] {
        discardChanges();
    });
    connect(layerTabs_, &QTabBar::currentChanged, this, [this](const int layer) {
        keyboardWidget_->setLayer(static_cast<std::size_t>(layer));
    });
    connect(keyboardWidget_, &KeyboardWidget::keyActivated, this, [this](const std::size_t slot) {
        editKey(slot);
    });

    actionLayout->addWidget(refreshButton_);
    actionLayout->addWidget(importButton_);
    actionLayout->addWidget(exportButton_);
    actionLayout->addStretch();
    actionLayout->addWidget(defaultsButton_);
    actionLayout->addWidget(discardButton_);
    actionLayout->addWidget(applyButton);
    pageLayout->addLayout(actionLayout);

    setCentralWidget(centralWidget);
    updateActions();
}

void MainWindow::beginDeviceScan()
{
    if (busy_) {
        return;
    }

    setBusy(true);
    connectionStatus_->setText(QStringLiteral("Searching…"));
    connectionStatus_->setProperty("state", QStringLiteral("searching"));
    connectionStatus_->style()->unpolish(connectionStatus_);
    connectionStatus_->style()->polish(connectionStatus_);
    if (!profileLoaded_) {
        showPlaceholder(
            QStringLiteral("Looking for an HHKB Studio"),
            QStringLiteral("Checking available HID interfaces…"));
    }

    const QPointer<MainWindow> window(this);
    QThreadPool::globalInstance()->start([window] {
        ScanResult result;
        const auto interfaces =
            hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces();
        if (interfaces.empty()) {
            result.status = ScanResult::Status::NotFound;
        } else {
            bool hasPermissionError = false;
            QString permissionPath;
            QString lastError;

            for (const auto& interface : interfaces) {
                if (!interface.canReadWrite) {
                    hasPermissionError = true;
                    if (permissionPath.isEmpty()) {
                        permissionPath = QString::fromStdString(interface.path.string());
                    }
                    continue;
                }

                try {
                    hhkbs::device::HidrawTransport transport(interface.path);
                    hhkbs::device::HhkbStudioDevice device(transport);
                    if (device.readProductName() != "HHKB-Studio") {
                        continue;
                    }

                    const auto information = device.readInformation();
                    result.profileBytes = device.readCurrentProfile();
                    result.status = ScanResult::Status::Connected;
                    result.devicePath = QString::fromStdString(interface.path.string());
                    result.productName = text(information.productName);
                    result.modelName = text(information.modelName);
                    result.keyboardLayout = text(information.keyboardLayout);
                    result.firmwareVersion = text(information.firmwareVersion);
                    result.currentProfile = information.currentProfile;
                    break;
                } catch (const std::exception& error) {
                    lastError = QString::fromLocal8Bit(error.what());
                }
            }

            if (result.status != ScanResult::Status::Connected) {
                if (hasPermissionError) {
                    result.status = ScanResult::Status::PermissionDenied;
                    result.devicePath = permissionPath;
                } else {
                    result.status = ScanResult::Status::Failed;
                    result.errorMessage = lastError.isEmpty()
                        ? QStringLiteral(
                              "No HHKB Studio configuration interface responded.")
                        : lastError;
                }
            }
        }

        if (!window) {
            return;
        }
        QMetaObject::invokeMethod(
            window,
            [window, result = std::move(result)]() mutable {
                if (window) {
                    window->applyScanResult(std::move(result));
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::applyScanResult(ScanResult result)
{
    setBusy(false);

    switch (result.status) {
    case ScanResult::Status::Connected: {
        try {
            keymap_.load(result.profileBytes);
            profileLoaded_ = true;
        } catch (const std::exception& error) {
            connectionStatus_->setText(QStringLiteral("Profile error"));
            connectionStatus_->setProperty("state", QStringLiteral("error"));
            showPlaceholder(
                QStringLiteral("The profile could not be loaded"),
                QString::fromLocal8Bit(error.what()));
            break;
        }

        connectionStatus_->setText(QStringLiteral("Connected"));
        connectionStatus_->setProperty("state", QStringLiteral("connected"));
        showEditor(
            QStringLiteral("%1 · %2 · Firmware %3 · Profile %4")
                .arg(
                    result.modelName.isEmpty() ? QStringLiteral("HHKB Studio") : result.modelName,
                    result.keyboardLayout.isEmpty()
                        ? QStringLiteral("US")
                        : result.keyboardLayout,
                    result.firmwareVersion.isEmpty()
                        ? QStringLiteral("Unknown")
                        : result.firmwareVersion)
                .arg(result.currentProfile + 1));
        break;
    }
    case ScanResult::Status::NotFound:
        connectionStatus_->setText(QStringLiteral("No device"));
        connectionStatus_->setProperty("state", QStringLiteral("idle"));
        if (!profileLoaded_) {
            showPlaceholder(
                QStringLiteral("Connect an HHKB Studio"),
                QStringLiteral(
                    "No supported keyboard was found. Connect it, import a TOML profile, "
                    "or run hhkbs with --demo."));
        }
        break;
    case ScanResult::Status::PermissionDenied:
        connectionStatus_->setText(QStringLiteral("Permission required"));
        connectionStatus_->setProperty("state", QStringLiteral("error"));
        if (!profileLoaded_) {
            showPlaceholder(
                QStringLiteral("Device access is blocked"),
                QStringLiteral(
                    "HHKB Studio was found at %1, but read/write access is unavailable.\n"
                    "Install the packaged udev rule and reconnect the keyboard.")
                    .arg(result.devicePath));
        }
        break;
    case ScanResult::Status::Failed:
        connectionStatus_->setText(QStringLiteral("Connection failed"));
        connectionStatus_->setProperty("state", QStringLiteral("error"));
        if (!profileLoaded_) {
            showPlaceholder(
                QStringLiteral("Could not read the keyboard"),
                result.errorMessage);
        }
        break;
    }

    connectionStatus_->style()->unpolish(connectionStatus_);
    connectionStatus_->style()->polish(connectionStatus_);
    updateActions();
}

void MainWindow::setBusy(const bool busy)
{
    busy_ = busy;
    updateActions();
}

void MainWindow::showEditor(const QString& summary)
{
    workspaceTitle_->setVisible(false);
    workspaceBody_->setVisible(false);
    keyboardWidget_->setKeymap(&keymap_);
    keyboardWidget_->setLayer(static_cast<std::size_t>(layerTabs_->currentIndex()));
    keyboardWidget_->setVisible(true);
    profileSummary_->setText(summary);
    profileSummary_->setVisible(true);
    layerTabs_->setEnabled(true);
}

void MainWindow::showPlaceholder(const QString& title, const QString& body)
{
    keyboardWidget_->setVisible(false);
    keyboardWidget_->setKeymap(nullptr);
    profileSummary_->setVisible(false);
    workspaceTitle_->setText(title);
    workspaceTitle_->setVisible(true);
    workspaceBody_->setText(body);
    workspaceBody_->setVisible(true);
    layerTabs_->setEnabled(false);
}

void MainWindow::editKey(const std::size_t slot)
{
    if (!profileLoaded_) {
        return;
    }

    const auto layer = static_cast<std::size_t>(layerTabs_->currentIndex());
    KeyAssignmentDialog dialog(keymap_.scanCode(layer, slot), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    keymap_.setScanCode(layer, slot, dialog.selectedScanCode());
    keyboardWidget_->update();
    updateActions();
}

void MainWindow::importProfile()
{
    if (!confirmDiscardChanges()) {
        return;
    }

    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import HHKB profile"),
        {},
        QStringLiteral("TOML profiles (*.toml);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(
            this,
            QStringLiteral("Could not open profile"),
            file.errorString());
        return;
    }

    try {
        const auto data = file.readAll();
        keymap_ = hhkbs::keymap::ProfileSerializer::fromToml(
            std::string_view(data.constData(), static_cast<std::size_t>(data.size())));
        profileLoaded_ = true;
        connectionStatus_->setText(QStringLiteral("Offline profile"));
        connectionStatus_->setProperty("state", QStringLiteral("idle"));
        showEditor(QFileInfo(path).fileName());
        updateActions();
    } catch (const std::exception& error) {
        QMessageBox::critical(
            this,
            QStringLiteral("Invalid profile"),
            QString::fromLocal8Bit(error.what()));
    }
}

bool MainWindow::exportProfile()
{
    if (!profileLoaded_) {
        return false;
    }

    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export HHKB profile"),
        QStringLiteral("profile.toml"),
        QStringLiteral("TOML profiles (*.toml)"));
    if (path.isEmpty()) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(
            this,
            QStringLiteral("Could not export profile"),
            file.errorString());
        return false;
    }

    const auto document = hhkbs::keymap::ProfileSerializer::toToml(keymap_);
    if (file.write(document.data(), static_cast<qint64>(document.size()))
            != static_cast<qint64>(document.size())
        || !file.commit()) {
        QMessageBox::critical(
            this,
            QStringLiteral("Could not export profile"),
            file.errorString());
        return false;
    }
    return true;
}

void MainWindow::restoreFactoryDefaults()
{
    if (!profileLoaded_) {
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Restore factory defaults"),
        QStringLiteral(
            "Replace the editor contents with the HHKB Studio US Profile 1 "
            "factory defaults?\n\nThe keyboard will not be changed until Apply to "
            "keyboard is available and selected."),
        QMessageBox::RestoreDefaults | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::RestoreDefaults) {
        return;
    }

    const hhkbs::keymap::Keymap defaults(
        hhkbs::keymap::KeyboardLayout::usWindowsFactoryProfile());
    for (std::size_t layer = 0; layer < hhkbs::keymap::Keymap::layerCount; ++layer) {
        for (std::size_t slot = 0;
             slot < hhkbs::keymap::Keymap::keysPerLayer;
             ++slot) {
            keymap_.setScanCode(layer, slot, defaults.scanCode(layer, slot));
        }
    }
    keyboardWidget_->update();
    updateActions();
}

void MainWindow::discardChanges()
{
    if (!profileLoaded_ || !keymap_.isModified()) {
        return;
    }
    keymap_.reset();
    keyboardWidget_->update();
    updateActions();
}

bool MainWindow::confirmDiscardChanges()
{
    if (!profileLoaded_ || !keymap_.isModified()) {
        return true;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Unsaved keymap changes"),
        QStringLiteral("Export the modified profile before replacing it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return exportProfile();
    }
    return true;
}

void MainWindow::updateActions()
{
    if (refreshButton_ == nullptr) {
        return;
    }
    refreshButton_->setEnabled(!busy_);
    importButton_->setEnabled(!busy_);
    exportButton_->setEnabled(!busy_ && profileLoaded_);
    defaultsButton_->setEnabled(!busy_ && profileLoaded_);
    discardButton_->setEnabled(!busy_ && profileLoaded_ && keymap_.isModified());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}
