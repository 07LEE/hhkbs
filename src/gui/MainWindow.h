#pragma once

#include "keymap/Keymap.h"

#include <QMainWindow>

class QFrame;
class QLabel;
class QPushButton;
class QTabBar;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    struct ScanResult;

    void buildInterface();
    void beginDeviceScan();
    void applyScanResult(ScanResult result);
    void setBusy(bool busy);

    QLabel* connectionStatus_{};
    QLabel* workspaceTitle_{};
    QLabel* workspaceBody_{};
    QTabBar* layerTabs_{};
    QPushButton* refreshButton_{};
    QFrame* workspace_{};
    hhkbs::keymap::Keymap keymap_;
    bool busy_{};
};
