#pragma once
#include "gui/KeyAssignmentDialog.h"
#include "keymap/BackupFiles.h"
#include "keymap/Keymap.h"
#include <array>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

class MainWindow final {
public:
    explicit MainWindow(bool demoMode = false);
    void draw();
    void requestClose();
    [[nodiscard]] bool shouldClose() const { return close_; }
private:
    enum class Action { None, Read, SwitchProfile, Import, Restore, Close };
    enum class Dialog { None, Assign, Unsaved, Import, Export, Overwrite, Defaults, Apply, Backups };
    struct ScanResult {
        std::string status;
        std::string detail;
        std::vector<std::uint8_t> bytes;
        std::optional<std::uint16_t> profile;
    };
    struct ApplyResult {
        bool ok = false;
        std::string message;
        std::vector<std::uint8_t> bytes;
        std::uint16_t profile = 0;
    };
    void beginScan(std::optional<std::uint16_t> profile = std::nullopt);
    void selectProfile(std::uint16_t profile);
    void pollScan();
    void beginApply();
    void pollApply();
    [[nodiscard]] bool busy() const { return scan_.valid() || apply_.valid(); }
    void request(Action action);
    void perform(Action action);
    void openFiles(bool save);
    void openBackups();
    void drawBackups();
    [[nodiscard]] bool loadBackup(const hhkbs::keymap::BackupEntry& entry);
    void drawDialog();
    void drawFiles();
    void saveFile(bool overwrite);
    void finishDialog();
    [[nodiscard]] bool unsaved() const;

    hhkbs::keymap::Keymap keymap_;
    std::vector<std::uint8_t> savedBytes_;
    std::vector<hhkbs::keymap::BackupEntry> backups_;
    std::optional<std::size_t> backupChoice_;
    std::future<ScanResult> scan_;
    std::future<ApplyResult> apply_;
    // The keyboard profile shown in the editor; only set once it has been read or applied.
    std::optional<std::uint16_t> selectedProfile_;
    std::optional<std::uint16_t> requestedProfile_;
    bool demo_ = false;
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
