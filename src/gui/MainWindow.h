#pragma once

#include "keymap/Keymap.h"

#include <QMainWindow>

class QFrame;
class QCloseEvent;
class QLabel;
class QPushButton;
class QTabBar;
class KeyboardWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(bool demoMode = false, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct ScanResult;

    void buildInterface();
    void beginDeviceScan();
    void applyScanResult(ScanResult result);
    void setBusy(bool busy);
    void showEditor(const QString& summary);
    void showPlaceholder(const QString& title, const QString& body);
    void editKey(std::size_t slot);
    void importProfile();
    [[nodiscard]] bool exportProfile();
    void discardChanges();
    [[nodiscard]] bool confirmDiscardChanges();
    void updateActions();

    QLabel* connectionStatus_{};
    QLabel* workspaceTitle_{};
    QLabel* workspaceBody_{};
    QTabBar* layerTabs_{};
    QPushButton* refreshButton_{};
    QPushButton* importButton_{};
    QPushButton* exportButton_{};
    QPushButton* resetButton_{};
    QFrame* workspace_{};
    QLabel* profileSummary_{};
    KeyboardWidget* keyboardWidget_{};
    hhkbs::keymap::Keymap keymap_;
    bool busy_{};
    bool profileLoaded_{};
};
