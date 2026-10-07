#pragma once
#include "app/KeyboardTasks.h"
#include "app/ProfileWorkspace.h"
#include "device/PadMonitor.h"
#include "gui/KeyAssignmentDialog.h"
#include "keymap/BackupFiles.h"
#include "keymap/Keymap.h"
#include "keymap/ProfileDiff.h"
#include <array>
#include <chrono>
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
    // Called with the files dropped on the window; the first .toml one is imported once nothing else is going on.
    void dropFiles(const std::vector<std::filesystem::path>& paths);
    [[nodiscard]] bool shouldClose() const { return close_; }
private:
    enum class Action { None, Read, LoadFile, LoadBackup, Close };
    enum class Dialog { None, Assign, Unsaved, Save, Defaults, Apply, Backups, CleanBackups };
    enum class BackupTab { Import, Restore, Manage };
    using ScanResult = hhkbs::app::ScanResult;
    using PadResult = hhkbs::app::PadResult;
    using ApplyResult = hhkbs::app::ApplyResult;
    using ProfileRead = hhkbs::app::ProfileRead;
    using PreviewResult = hhkbs::app::PreviewResult;
    // What the Apply dialog knows about one profile: the keys that differ from what the keyboard holds.
    struct Preview {
        bool read = false;
        std::string error;
        std::vector<hhkbs::keymap::KeyChange> changes;
    };
    void beginScan(std::optional<std::uint16_t> profile = std::nullopt, bool reconnect = false);
    void selectProfile(std::uint16_t profile);
    void pollScan();
    void beginPadChange(std::size_t pad, bool on);
    void pollPadChange();
    void pollConnection();
    void beginApply();
    void pollApply();
    void beginPreview();
    void pollPreview();
    void drawChanges();
    [[nodiscard]] bool busy() const { return scan_.valid() || apply_.valid() || pad_.valid() || preview_.valid(); }
    void request(Action action);
    void perform(Action action);
    void requestImport(const std::filesystem::path& path);
    [[nodiscard]] bool importFile(const std::filesystem::path& path);
    void pollDrop();
    void openSave();
    void drawSave();
    void saveBackup();
    void openBackups(bool manage = false);
    void openApply();
    void drawBackups();
    void drawBackupList(float belowList);
    void openBackupFolder();
    void requestLoadBackup(const hhkbs::keymap::BackupEntry& entry);
    void drawDeleteBackup();
    void saveBackupTag();
    void drawCleanBackups();
    [[nodiscard]] bool loadBackup(const hhkbs::keymap::BackupEntry& entry);
    void drawDialog();
    void drawImportTab(float belowList);
    void finishDialog();
    void cancelDialog();

    std::vector<hhkbs::keymap::BackupEntry> backups_;
    std::optional<std::size_t> backupChoice_;
    std::array<char, 256> tagInput_{};  // the tag being edited for the chosen backup
    std::optional<std::size_t> tagShownFor_;  // the backup tagInput_ was filled from
    std::optional<hhkbs::keymap::BackupEntry> pendingBackup_;
    std::optional<BackupTab> selectTab_;  // the Backups tab to come up on
    bool confirmDelete_ = false;  // the delete confirmation is open over the Backups window
    int keepBackups_ = 5;
    std::future<ScanResult> scan_;
    std::future<ApplyResult> apply_;
    std::future<PadResult> pad_;
    // The Apply dialog reads the profiles that have changes, so it can list what would change on each.
    std::future<PreviewResult> preview_;
    bool previewDone_ = false;
    std::array<Preview, 4> previews_;
    std::array<bool, 4> applyPick_{};   // the profiles the Apply will write
    std::uint16_t applyView_ = 0;       // the profile whose changes the dialog lists
    hhkbs::app::ProfileWorkspace work_;  // the profile on screen and the work kept for the others
    bool demo_ = false;
    std::string status_ = "No device";
    std::string message_;
    std::string dialogError_;
    bool close_ = false;
    hhkbs::device::PadMonitor pads_;  // gesture pad on/off as the keyboard reports it
    bool wasListening_ = false;       // the monitor was running, so it stopping means the keyboard went away
    bool bluetooth_ = false;          // the keyboard was read over Bluetooth: applying is left to USB
    bool disconnected_ = false;       // the keyboard went away; scans run quietly until it answers again
    bool reconnectScan_ = false;      // the running scan is such a probe: it only restores the connection
    std::chrono::steady_clock::time_point nextProbe_;
    std::vector<std::filesystem::path> seenPaths_;  // the interfaces seen at the last probe
    std::chrono::steady_clock::time_point settledAt_;  // before this the interfaces may still be coming up
    std::size_t layer_ = 0;
    std::size_t slot_ = 0;
    Dialog dialog_ = Dialog::None;
    Action pending_ = Action::None;
    KeyAssignmentDialog assignment_;
    std::array<char, 4096> path_{};      // the file an Import will read
    std::array<char, 4096> dirInput_{};  // the editable folder bar; follows directory_ until edited
    std::array<char, 256> saveTag_{};    // the tag typed in the Save dialog
    std::filesystem::path shownDir_;
    std::filesystem::path pendingFile_;  // the file to import once unsaved edits have been dealt with
    std::filesystem::path droppedFile_;  // dropped on the window, taken up at the start of the next frame
    std::filesystem::path importPath_;  // set by a double-click to import without pressing the button
    std::filesystem::path directory_;
};
