#pragma once
#include "gui/KeyAssignmentDialog.h"
#include "keymap/Keymap.h"
#include <array>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

class MainWindow final {
public:
    explicit MainWindow(bool demoMode = false);
    void draw();
    void requestClose();
    [[nodiscard]] bool shouldClose() const { return close_; }
private:
    enum class Action { None, Read, Import, Close };
    enum class Dialog { None, Assign, Unsaved, Import, Export, Overwrite, Defaults };
    struct ScanResult {
        std::string status;
        std::string detail;
        std::vector<std::uint8_t> bytes;
    };
    void beginScan();
    void pollScan();
    void request(Action action);
    void perform(Action action);
    void openFiles(bool save);
    void drawDialog();
    void drawFiles();
    void saveFile(bool overwrite);
    void finishDialog();
    [[nodiscard]] bool unsaved() const;

    hhkbs::keymap::Keymap keymap_;
    std::vector<std::uint8_t> savedBytes_;
    std::future<ScanResult> scan_;
    std::string status_ = "No device";
    std::string summary_;
    std::string message_;
    std::string dialogError_;
    bool loaded_ = false;
    bool close_ = false;
    std::size_t layer_ = 0;
    std::size_t slot_ = 0;
    Dialog dialog_ = Dialog::None;
    Action pending_ = Action::None;
    KeyAssignmentDialog assignment_;
    std::array<char, 4096> path_{};
    std::filesystem::path directory_;
};
