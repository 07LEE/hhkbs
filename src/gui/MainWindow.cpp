#include "gui/MainWindow.h"

#include "device/DeviceDiscovery.h"
#include "device/HhkbStudioDevice.h"
#include "device/HidrawTransport.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildInterface();
    beginDeviceScan();
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("HHKBS"));
    setMinimumSize(900, 560);
    resize(1120, 700);

    auto* centralWidget = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(centralWidget);
    pageLayout->setContentsMargins(32, 28, 32, 28);
    pageLayout->setSpacing(22);

    auto* headerLayout = new QHBoxLayout;
    auto* titleLayout = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("HHKBS"));
    title->setObjectName(QStringLiteral("title"));
    auto* subtitle = new QLabel(QStringLiteral("HHKB Studio keymap editor"));
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

    layerTabs_ = new QTabBar;
    layerTabs_->setObjectName(QStringLiteral("layerTabs"));
    layerTabs_->addTab(QStringLiteral("Base"));
    layerTabs_->addTab(QStringLiteral("Fn1"));
    layerTabs_->addTab(QStringLiteral("Fn2"));
    layerTabs_->addTab(QStringLiteral("Fn3"));
    layerTabs_->setExpanding(false);
    layerTabs_->setEnabled(false);
    pageLayout->addWidget(layerTabs_);

    workspace_ = new QFrame;
    workspace_->setObjectName(QStringLiteral("workspace"));
    workspace_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* workspaceLayout = new QVBoxLayout(workspace_);
    workspaceLayout->setAlignment(Qt::AlignCenter);

    workspaceTitle_ = new QLabel(QStringLiteral("Looking for an HHKB Studio"));
    workspaceTitle_->setObjectName(QStringLiteral("placeholderTitle"));
    workspaceTitle_->setAlignment(Qt::AlignCenter);
    workspaceBody_ = new QLabel(QStringLiteral("Checking available HID interfaces…"));
    workspaceBody_->setObjectName(QStringLiteral("placeholderBody"));
    workspaceBody_->setAlignment(Qt::AlignCenter);
    workspaceBody_->setWordWrap(true);
    workspaceBody_->setMaximumWidth(680);
    workspaceLayout->addWidget(workspaceTitle_);
    workspaceLayout->addWidget(workspaceBody_);
    pageLayout->addWidget(workspace_, 1);

    auto* actionLayout = new QHBoxLayout;
    refreshButton_ = new QPushButton(QStringLiteral("Read from keyboard"));
    auto* importButton = new QPushButton(QStringLiteral("Import"));
    auto* exportButton = new QPushButton(QStringLiteral("Export"));
    auto* resetButton = new QPushButton(QStringLiteral("Discard changes"));
    auto* applyButton = new QPushButton(QStringLiteral("Apply to keyboard"));
    applyButton->setObjectName(QStringLiteral("primaryButton"));

    importButton->setEnabled(false);
    exportButton->setEnabled(false);
    resetButton->setEnabled(false);
    applyButton->setEnabled(false);

    connect(refreshButton_, &QPushButton::clicked, this, [this] {
        beginDeviceScan();
    });

    actionLayout->addWidget(refreshButton_);
    actionLayout->addWidget(importButton);
    actionLayout->addWidget(exportButton);
    actionLayout->addStretch();
    actionLayout->addWidget(resetButton);
    actionLayout->addWidget(applyButton);
    pageLayout->addLayout(actionLayout);

    setCentralWidget(centralWidget);
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
    workspaceTitle_->setText(QStringLiteral("Looking for an HHKB Studio"));
    workspaceBody_->setText(QStringLiteral("Checking available HID interfaces…"));
    layerTabs_->setEnabled(false);

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
        } catch (const std::exception& error) {
            connectionStatus_->setText(QStringLiteral("Profile error"));
            connectionStatus_->setProperty("state", QStringLiteral("error"));
            workspaceTitle_->setText(QStringLiteral("The profile could not be loaded"));
            workspaceBody_->setText(QString::fromLocal8Bit(error.what()));
            layerTabs_->setEnabled(false);
            break;
        }

        connectionStatus_->setText(QStringLiteral("Connected"));
        connectionStatus_->setProperty("state", QStringLiteral("connected"));
        workspaceTitle_->setText(
            result.productName.isEmpty()
                ? QStringLiteral("HHKB Studio")
                : result.productName);
        workspaceBody_->setText(
            QStringLiteral(
                "Device: %1\nModel: %2 · Layout: %3 · Firmware: %4\n"
                "Profile %5 loaded — 4 layers, 120 scan-code slots per layer")
                .arg(
                    result.devicePath,
                    result.modelName.isEmpty() ? QStringLiteral("Unknown") : result.modelName,
                    result.keyboardLayout.isEmpty()
                        ? QStringLiteral("Unknown")
                        : result.keyboardLayout,
                    result.firmwareVersion.isEmpty()
                        ? QStringLiteral("Unknown")
                        : result.firmwareVersion)
                .arg(result.currentProfile + 1));
        layerTabs_->setEnabled(true);
        break;
    }
    case ScanResult::Status::NotFound:
        connectionStatus_->setText(QStringLiteral("No device"));
        connectionStatus_->setProperty("state", QStringLiteral("idle"));
        workspaceTitle_->setText(QStringLiteral("Connect an HHKB Studio"));
        workspaceBody_->setText(
            QStringLiteral(
                "No supported HHKB Studio was found. Connect it over USB or Bluetooth, "
                "then select “Read from keyboard”."));
        layerTabs_->setEnabled(false);
        break;
    case ScanResult::Status::PermissionDenied:
        connectionStatus_->setText(QStringLiteral("Permission required"));
        connectionStatus_->setProperty("state", QStringLiteral("error"));
        workspaceTitle_->setText(QStringLiteral("Device access is blocked"));
        workspaceBody_->setText(
            QStringLiteral(
                "HHKB Studio was found at %1, but read/write access is unavailable.\n"
                "Install a udev rule for USB vendor 04fe and product 0016, "
                "then reconnect the keyboard.")
                .arg(result.devicePath));
        layerTabs_->setEnabled(false);
        break;
    case ScanResult::Status::Failed:
        connectionStatus_->setText(QStringLiteral("Connection failed"));
        connectionStatus_->setProperty("state", QStringLiteral("error"));
        workspaceTitle_->setText(QStringLiteral("Could not read the keyboard"));
        workspaceBody_->setText(result.errorMessage);
        layerTabs_->setEnabled(false);
        break;
    }

    connectionStatus_->style()->unpolish(connectionStatus_);
    connectionStatus_->style()->polish(connectionStatus_);
}

void MainWindow::setBusy(const bool busy)
{
    busy_ = busy;
    if (refreshButton_ != nullptr) {
        refreshButton_->setEnabled(!busy);
    }
}
