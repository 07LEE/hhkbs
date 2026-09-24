#include "gui/MainWindow.h"
#include "gui/Theme.h"
#include "gui/KeyboardWidget.h"
#include "device/DeviceDiscovery.h"
#include "device/HidrawTransport.h"
#include "device/HhkbStudioDevice.h"
#include "keymap/BackupFiles.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ProfileFiles.h"
#include "keymap/ProfileSerializer.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <stdexcept>
#include <utility>

using hhkbs::keymap::Keymap;
using hhkbs::keymap::KeyboardLayout;

namespace {

std::unique_ptr<hhkbs::device::HidrawTransport> openStudio()
{
    for (const auto& item : hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces()) {
        if (!item.canReadWrite) continue;
        try {
            auto transport = std::make_unique<hhkbs::device::HidrawTransport>(item.path);
            if (hhkbs::device::HhkbStudioDevice(*transport).readProductName() == "HHKB-Studio")
                return transport;
        } catch (const std::exception&) {}
    }
    throw std::runtime_error("No writable HHKB Studio was found. Check the connection and udev rules.");
}

// Runs xdg-open in the background. The path is passed as an argument, never spliced into a command line.
bool openFolder(const std::filesystem::path& folder)
{
    const std::string command = "xdg-open \"$1\" >/dev/null 2>&1 &";
    const char* argv[] = {"sh", "-c", command.c_str(), "sh", folder.c_str(), nullptr};
    pid_t child = 0;
    if (::posix_spawnp(&child, "sh", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
    int status = 0;
    return ::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool commandExists(const char* name)
{
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string list = path;
    for (std::size_t start = 0; start <= list.size();) {
        const auto end = std::min(list.find(':', start), list.size());
        if (::access((list.substr(start, end - start) + "/" + name).c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}

}  // namespace

MainWindow::MainWindow(bool demoMode)
    : demo_(demoMode)
{
    std::error_code error;
    directory_ = std::filesystem::current_path(error);
    if (error) directory_ = "/";
    if (const char* home = std::getenv("HOME"); home && std::filesystem::is_directory(home, error))
        directory_ = home;
    if (demoMode) {
        keymap_.load(KeyboardLayout::demoProfile());
        savedBytes_ = keymap_.toBytes();
        loaded_ = true;
        status_ = "Demo profile";
        summary_ = "Offline US profile / Changes are kept in memory";
    } else beginScan();
    refreshBackupCount();
}

void MainWindow::refreshBackupCount()
{
    backupCount_ = hhkbs::keymap::listBackups(hhkbs::keymap::backupDirectory()).size();
}

bool MainWindow::unsaved() const { return loaded_ && keymap_.toBytes() != savedBytes_; }

void MainWindow::beginScan(std::optional<std::uint16_t> target)
{
    if (scan_.valid()) return;
    status_ = "Searching...";
    message_ = "Checking available HID interfaces...";
    scan_ = std::async(std::launch::async, [target] {
        ScanResult result{"No device", "Connect an HHKB Studio, import a TOML profile, or start with --demo.", {}, std::nullopt};
        try {
            const auto devices = hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces();
            bool permission = false;
            std::string lastError;
            for (const auto& item : devices) {
                if (!item.canReadWrite) { permission = true; continue; }
                try {
                    hhkbs::device::HidrawTransport transport(item.path);
                    hhkbs::device::HhkbStudioDevice device(transport);
                    if (device.readProductName() != "HHKB-Studio") continue;
                    const auto info = device.readInformation();
                    const auto profile = target.value_or(info.currentProfile);
                    result.bytes = device.readProfile(profile);
                    result.profile = profile;
                    result.status = "Connected";
                    result.detail = info.modelName + " / " + info.keyboardLayout +
                        " / Firmware " + info.firmwareVersion + " / Profile " + std::to_string(profile+1);
                    if (profile != info.currentProfile)
                        result.detail += " (keyboard is on Profile " + std::to_string(info.currentProfile+1) + ")";
                    return result;
                } catch (const std::exception& error) { lastError = error.what(); }
            }
            if (permission) {
                result.status = "Permission required";
                result.detail = "Install packaging/60-hhkbs.rules as described in the README, then reconnect the keyboard.";
            } else if (!devices.empty()) {
                result.status = "Connection failed";
                result.detail = lastError.empty() ? "No configuration interface responded." : lastError;
            }
        } catch (const std::exception& error) {
            result.status = "Connection failed";
            result.detail = error.what();
        }
        return result;
    });
}

void MainWindow::pollScan()
{
    if (!scan_.valid() || scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        auto result = scan_.get();
        status_ = result.status;
        message_ = result.detail;
        if (!result.bytes.empty()) {
            Keymap profile(result.bytes);
            keymap_ = std::move(profile);
            savedBytes_ = keymap_.toBytes();
            loaded_ = true;
            selectedProfile_ = result.profile;
            summary_ = result.detail;
            message_.clear();
        }
    } catch (const std::exception& error) { status_ = "Profile error"; message_ = error.what(); }
}

void MainWindow::beginApply()
{
    if (busy() || demo_ || !loaded_ || !applyTarget_) return;
    status_ = "Applying...";
    message_ = "Writing the profile to the keyboard. Do not unplug it.";
    auto bytes = keymap_.toBytes();
    const std::uint16_t profile = *applyTarget_;
    apply_ = std::async(std::launch::async, [bytes = std::move(bytes), profile] {
        ApplyResult result{false, {}, bytes, 0};
        try {
            auto transport = openStudio();
            hhkbs::device::HhkbStudioDevice device(*transport);
            std::filesystem::path path;
            device.runOnProfile(profile, [&] {
                device.requireTarget(profile);
                const auto backup = device.readCurrentProfile();

                // Keep a copy of what the keyboard held; without it a failed write cannot be undone by hand.
                const auto directory = hhkbs::keymap::backupDirectory();
                std::filesystem::create_directories(directory);
                path = directory / hhkbs::keymap::backupFileName(std::time(nullptr), profile);
                hhkbs::keymap::writeProfile(path, hhkbs::keymap::Keymap(backup), false);

                device.writeCurrentProfile(bytes, backup);
            });
            result.ok = true;
            result.profile = profile;
            result.message = "Applied to profile " + std::to_string(profile + 1) + ". The previous profile was saved as " + path.filename().string() + ".";
        } catch (const std::exception& error) { result.message = error.what(); }
        return result;
    });
}

void MainWindow::pollApply()
{
    if (!apply_.valid() || apply_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        const auto result = apply_.get();
        message_ = result.message;
        if (result.ok) {
            keymap_ = Keymap(result.bytes);
            savedBytes_ = keymap_.toBytes();
            selectedProfile_ = result.profile;
            status_ = "Applied";
            refreshBackupCount();
        } else status_ = "Apply failed";
    } catch (const std::exception& error) { status_ = "Apply failed"; message_ = error.what(); }
}

void MainWindow::requestClose()
{
    if (busy()) { message_ = "Please wait for the keyboard operation to finish before closing."; return; }
    if (dialog_ == Dialog::None) request(Action::Close);
}
void MainWindow::selectProfile(std::uint16_t profile)
{
    if (busy() || demo_ || profile == selectedProfile_) return;
    requestedProfile_ = profile;
    request(Action::SwitchProfile);
}
void MainWindow::request(Action action)
{
    if (unsaved()) { pending_ = action; dialog_ = Dialog::Unsaved; }
    else perform(action);
}
void MainWindow::perform(Action action)
{
    if (action == Action::Read) beginScan();
    else if (action == Action::SwitchProfile) beginScan(requestedProfile_);
    else if (action == Action::Import) openFiles(false);
    else if (action == Action::LoadBackup && pendingBackup_) {
        const auto entry = *pendingBackup_;
        pendingBackup_.reset();
        if (!loadBackup(entry)) message_ = dialogError_;
        else if (pendingBackupApply_) openApply();
    }
    else if (action == Action::Close) close_ = true;
}
void MainWindow::openFiles(bool save)
{
    dialog_ = save ? Dialog::Export : Dialog::Import;
    dialogError_.clear();
    path_[0] = '\0';
    std::snprintf(fileName_.data(), fileName_.size(), "%s", save ? "profile.toml" : "");
}
void MainWindow::openApply()
{
    applyTarget_ = selectedProfile_;
    dialogError_.clear();
    dialog_ = Dialog::Apply;
}
void MainWindow::openBackups(bool manage)
{
    backups_ = hhkbs::keymap::listBackups(hhkbs::keymap::backupDirectory());
    backupChoice_.reset();
    backupCount_ = backups_.size();
    dialogError_.clear();
    selectManageTab_ = manage;
    dialog_ = Dialog::Backups;
}
void MainWindow::requestLoadBackup(const hhkbs::keymap::BackupEntry& entry, bool thenApply)
{
    // Loading replaces the editor, so unsaved edits get the usual chance to be exported first.
    pendingBackup_ = entry;
    pendingBackupApply_ = thenApply;
    request(Action::LoadBackup);
}
bool MainWindow::loadBackup(const hhkbs::keymap::BackupEntry& entry)
{
    try {
        auto profile = hhkbs::keymap::readProfile(entry.path);
        keymap_ = std::move(profile);
        savedBytes_ = keymap_.toBytes();
        loaded_ = true;
        selectedProfile_ = entry.profile;
        summary_ = "Backup of Profile " + std::to_string(entry.profile + 1) + " from " + entry.timestamp;
        status_ = "Loaded backup";
        message_.clear();
        finishDialog();
        return true;
    } catch (const std::exception& error) { dialogError_ = error.what(); }
    return false;
}
void MainWindow::drawBackupList()
{
    ImGui::TextDisabled("%s", hhkbs::keymap::backupDirectory().c_str());
    ImGui::BeginChild("Backups", ImVec2(0, 230), ImGuiChildFlags_Borders);
    if (backups_.empty()) ImGui::TextWrapped("No backups yet.");
    for (std::size_t i=0; i<backups_.size(); ++i) {
        const auto& entry = backups_[i];
        const auto label = entry.timestamp + "    Profile " + std::to_string(entry.profile + 1);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), backupChoice_ == i)) backupChoice_ = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
}
void MainWindow::drawBackups()
{
    ImGui::TextUnformatted("Backups");
    ImGui::TextWrapped("A backup is saved before every apply.");
    if (!ImGui::BeginTabBar("BackupTabs")) return;
    if (ImGui::BeginTabItem("Restore")) {
        ImGui::TextWrapped("Choose one, then load it into the editor for the profile it came from or restore it to the keyboard right away.");
        drawBackupList();
        ImGui::BeginDisabled(!backupChoice_);
        if (ImGui::Button("Load into editor")) requestLoadBackup(backups_[*backupChoice_], false);
        ImGui::SameLine();
        ImGui::BeginDisabled(demo_);
        if (ImGui::Button("Restore and apply")) requestLoadBackup(backups_[*backupChoice_], true);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancelDialog();
        ImGui::EndTabItem();
    }
    // Coming back from a delete or clean-up lands on the tab the user left.
    const auto flags = selectManageTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    selectManageTab_ = false;
    if (ImGui::BeginTabItem("Manage", nullptr, flags)) {
        ImGui::TextWrapped("Delete the backups you no longer need. HHKBS never deletes them on its own.");
        drawBackupList();
        ImGui::BeginDisabled(!backupChoice_);
        if (ImGui::Button("Delete")) dialog_ = Dialog::DeleteBackup;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(backups_.empty());
        if (ImGui::Button("Clean up...")) dialog_ = Dialog::CleanBackups;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!std::filesystem::is_directory(hhkbs::keymap::backupDirectory()));
        if (ImGui::Button("Open folder")) {
            if (!commandExists("xdg-open"))
                dialogError_ = "xdg-open was not found. Open " + hhkbs::keymap::backupDirectory().string() + " yourself.";
            else if (!openFolder(hhkbs::keymap::backupDirectory())) dialogError_ = "Could not open the folder.";
            else dialogError_.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancelDialog();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
void MainWindow::drawCleanBackups()
{
    ImGui::TextUnformatted("Clean up backups");
    ImGui::TextWrapped("Keep the newest backups of each profile and delete the rest. This cannot be undone.");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("newest backups to keep per profile", &keepBackups_);
    keepBackups_ = std::clamp(keepBackups_, 1, 999);
    const auto surplus = hhkbs::keymap::backupsBeyondNewest(backups_, static_cast<std::size_t>(keepBackups_));
    ImGui::TextWrapped("%zu of %zu backups will be deleted.", surplus.size(), backups_.size());
    ImGui::BeginDisabled(surplus.empty());
    if (ImGui::Button("Delete backups")) {
        std::size_t deleted = 0;
        std::string firstError;
        for (const auto& entry : surplus) {
            try {
                hhkbs::keymap::deleteBackup(hhkbs::keymap::backupDirectory(), entry);
                ++deleted;
            } catch (const std::exception& error) { if (firstError.empty()) firstError = error.what(); }
        }
        openBackups(true);
        message_ = std::to_string(deleted) + " backup(s) deleted";
        dialogError_ = firstError;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Back")) { dialog_ = Dialog::Backups; selectManageTab_ = true; }
}
void MainWindow::drawDeleteBackup()
{
    const auto& entry = backups_[*backupChoice_];
    ImGui::TextUnformatted("Delete backup");
    ImGui::TextWrapped("Delete the backup of Profile %d saved on %s? This cannot be undone.", entry.profile + 1, entry.timestamp.c_str());
    if (ImGui::Button("Delete backup")) {
        try {
            hhkbs::keymap::deleteBackup(hhkbs::keymap::backupDirectory(), entry);
            openBackups(true);
        } catch (const std::exception& error) { dialogError_ = error.what(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) { dialog_ = Dialog::Backups; selectManageTab_ = true; }
}
void MainWindow::cancelDialog()
{
    pending_ = Action::None;
    finishDialog();
}
void MainWindow::finishDialog()
{
    dialog_ = Dialog::None;
    dialogError_.clear();
    ImGui::CloseCurrentPopup();
}
void MainWindow::saveFile(bool overwrite)
{
    try {
        auto target = std::filesystem::path(path_.data());
        if (target.extension().empty()) target += ".toml";
        if (target.filename().empty()) throw std::runtime_error("Choose a file name.");
        hhkbs::keymap::writeProfile(target, keymap_, overwrite);
        savedBytes_ = keymap_.toBytes();
        message_ = "Exported " + target.string();
        directory_ = target.parent_path();
        const auto action = std::exchange(pending_, Action::None);
        finishDialog();
        perform(action);
    } catch (const std::exception& error) { dialogError_ = error.what(); dialog_ = Dialog::Export; }
}

namespace {
std::string fileTime(const std::filesystem::file_time_type& time)
{
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    ::localtime_r(&seconds, &local);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", &local);
    return text;
}

std::string fileSize(std::uintmax_t bytes)
{
    char text[32];
    if (bytes < 1024) std::snprintf(text, sizeof text, "%ju B", bytes);
    else std::snprintf(text, sizeof text, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    return text;
}

void drawFolderIcon(ImDrawList* draw, ImVec2 topLeft, float height)
{
    const float w = height * 1.05f, h = height * .7f;
    const ImVec2 min(topLeft.x, topLeft.y + (height - h) / 2);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddRectFilled(min, ImVec2(min.x + w * .45f, min.y + h * .3f), color, 2.f);
    draw->AddRectFilled(ImVec2(min.x, min.y + h * .15f), ImVec2(min.x + w, min.y + h), color, 2.f);
}
}

void MainWindow::drawFiles()
{
    const bool save = dialog_ == Dialog::Export;
    const auto& style = ImGui::GetStyle();
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(save ? "Export TOML profile" : "Import TOML profile");
    ImGui::SetWindowFontScale(1.f);

    // Path bar: up button plus an editable folder; typing a .toml file path selects that file.
    if (shownDir_ != directory_) {
        std::snprintf(dirInput_.data(), dirInput_.size(), "%s", directory_.c_str());
        shownDir_ = directory_;
    }
    const auto goTo = [this](const std::filesystem::path& folder) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(folder, ec);
        directory_ = ec ? folder : canonical;
        path_[0] = '\0';
        dialogError_.clear();
    };
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) goTo(directory_.parent_path().empty() ? std::filesystem::path("/") : directory_.parent_path());
    ImGui::SetItemTooltip("Parent folder");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dir", dirInput_.data(), dirInput_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ec;
        const std::filesystem::path entered(dirInput_.data());
        if (std::filesystem::is_directory(entered, ec)) goTo(entered);
        else if (std::filesystem::is_regular_file(entered, ec)) {
            goTo(entered.parent_path());
            std::snprintf(path_.data(), path_.size(), "%s", (directory_ / entered.filename()).c_str());
            std::snprintf(fileName_.data(), fileName_.size(), "%s", entered.filename().c_str());
        } else dialogError_ = "That folder is unavailable.";
    }

    struct Entry { std::filesystem::path path; bool directory; std::string modified, size; };
    std::vector<Entry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it(directory_, error), end;
    while (!error && it != end) {
        std::error_code ec;
        const bool isDirectory = it->is_directory(ec);
        const bool hidden = it->path().filename().string().starts_with('.');
        if (!ec && !hidden && (isDirectory || it->path().extension() == ".toml")) {
            Entry entry{it->path(), isDirectory, {}, {}};
            std::error_code timeError, sizeError;
            const auto time = it->last_write_time(timeError);
            if (!timeError) entry.modified = fileTime(time);
            if (!isDirectory) { const auto bytes = it->file_size(sizeError); if (!sizeError) entry.size = fileSize(bytes); }
            entries.push_back(std::move(entry));
        }
        it.increment(error);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.directory != b.directory ? a.directory > b.directory : a.path.filename() < b.path.filename();
    });

    const std::filesystem::path chosen = save ? directory_ / fileName_.data() : std::filesystem::path(path_.data());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 4));
    ImGui::BeginChild("Files", ImVec2(0, 250), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("files", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableHeadersRow();
        const float iconWidth = ImGui::GetTextLineHeight() * 1.05f + 8.f;
        for (std::size_t i=0; i<entries.size(); ++i) {
            const auto& entry = entries[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 iconPos = ImGui::GetCursorScreenPos();
            ImGui::Indent(entry.directory ? iconWidth : 0.f);
            const auto name = entry.path.filename().string();
            const bool picked = !entry.directory && chosen == entry.path;
            const bool activated = ImGui::Selectable(name.c_str(), picked,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            ImGui::Unindent(entry.directory ? iconWidth : 0.f);
            if (entry.directory) drawFolderIcon(ImGui::GetWindowDrawList(), iconPos, ImGui::GetTextLineHeight());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.modified.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.size.c_str());
            ImGui::PopID();
            if (!activated) continue;
            const bool doubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (entry.directory) { if (doubleClick) goTo(entry.path); continue; }
            dialogError_.clear();
            std::snprintf(path_.data(), path_.size(), "%s", entry.path.c_str());
            std::snprintf(fileName_.data(), fileName_.size(), "%s", name.c_str());
            if (doubleClick && !save) {
                // Same path as the Import button below.
                importPath_ = entry.path;
            }
        }
        if (entries.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(error ? "Cannot list folder: %s" : "No folders or .toml files here", error.message().c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (save) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("File name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", fileName_.data(), fileName_.size());
    }
    if (!dialogError_.empty()) ImGui::TextWrapped("%s", dialogError_.c_str());

    // Footer: Cancel, then the one primary action, right-aligned.
    const char* action = save ? "Export" : "Import";
    const float buttonWidth = 110.f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - 2 * buttonWidth - style.ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) { cancelDialog(); return; }
    ImGui::SameLine();
    const bool ready = save ? fileName_[0] != '\0' : (path_[0] != '\0' || !importPath_.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::palette().accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::palette().accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().accentText);
    ImGui::BeginDisabled(!ready);
    const bool pressed = ImGui::Button(action, ImVec2(buttonWidth, 0));
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    if (!pressed && importPath_.empty()) return;
    try {
        if (save) {
            auto target = directory_ / fileName_.data();
            if (target.extension().empty()) target += ".toml";
            std::snprintf(path_.data(), path_.size(), "%s", target.c_str());
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::symlink_status(target, ec))) dialog_ = Dialog::Overwrite;
            else saveFile(false);
        } else {
            const auto target = importPath_.empty() ? std::filesystem::path(path_.data()) : importPath_;
            importPath_.clear();
            auto profile = hhkbs::keymap::readProfile(target);
            keymap_ = std::move(profile);
            savedBytes_ = keymap_.toBytes();
            loaded_ = true;
            selectedProfile_.reset();  // a file belongs to no keyboard profile until the user picks one
            summary_ = "Imported " + target.filename().string();
            status_ = "Imported profile";
            message_.clear();
            directory_ = target.parent_path();
            finishDialog();
        }
    } catch (const std::exception& error) { importPath_.clear(); dialogError_ = error.what(); }
}

void MainWindow::drawDialog()
{
    if (dialog_ == Dialog::None) return;
    if (!ImGui::IsPopupOpen("HHKBS")) ImGui::OpenPopup("HHKBS");
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(620,0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("HHKBS", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const bool filesDialog = dialog_ == Dialog::Import || dialog_ == Dialog::Export;
    const bool ownCancel = dialog_ == Dialog::Backups || filesDialog;  // these draw Cancel (and errors) themselves
    if (dialog_ == Dialog::Assign) {
        if (const auto code = assignment_.draw()) { keymap_.setScanCode(layer_, slot_, *code); finishDialog(); }
    } else if (dialog_ == Dialog::Unsaved) {
        ImGui::TextUnformatted("Unsaved keymap changes");
        ImGui::TextWrapped("Export the modified profile before continuing?");
        if (ImGui::Button("Export first")) openFiles(true);
        ImGui::SameLine();
        if (ImGui::Button("Discard and continue")) {
            const auto action = std::exchange(pending_, Action::None);
            finishDialog();
            perform(action);
        }
    } else if (dialog_ == Dialog::Import || dialog_ == Dialog::Export) drawFiles();
    else if (dialog_ == Dialog::Backups) drawBackups();
    else if (dialog_ == Dialog::DeleteBackup) drawDeleteBackup();
    else if (dialog_ == Dialog::CleanBackups) drawCleanBackups();
    else if (dialog_ == Dialog::Overwrite) {
        ImGui::TextWrapped("Replace the existing file?\n%s", path_.data());
        if (ImGui::Button("Replace file")) saveFile(true);
        ImGui::SameLine();
        if (ImGui::Button("Back")) dialog_ = Dialog::Export;
    } else if (dialog_ == Dialog::Apply) {
        ImGui::TextUnformatted("Apply to keyboard");
        ImGui::TextUnformatted("Profile to overwrite");
        for (std::uint16_t i=0; i<4; ++i) {
            if (i) ImGui::SameLine();
            const bool chosen = applyTarget_ && *applyTarget_ == i;
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().selected);
            const auto label = "Profile " + std::to_string(i+1);
            if (ImGui::Button(label.c_str(), ImVec2(96,32))) applyTarget_ = i;
            if (chosen) ImGui::PopStyleColor();
        }
        if (applyTarget_) {
            const auto target = "Profile " + std::to_string(*applyTarget_ + 1);
            ImGui::TextWrapped("The keyboard's current %s is saved as a backup first, and the result is read back to verify it. "
                               "The keyboard returns to the profile it was on afterwards. "
                               "Do not unplug the keyboard while writing.", target.c_str());
        } else ImGui::TextDisabled("Choose the profile to overwrite.");
        ImGui::BeginDisabled(!applyTarget_);
        if (ImGui::Button("Apply")) { finishDialog(); beginApply(); }
        ImGui::EndDisabled();
    } else if (dialog_ == Dialog::Defaults) {
        ImGui::TextWrapped("Restore the US Profile 1 defaults in the editor? "
                           "Restoring alone does not change the keyboard; use \"Restore and apply\" to write them right away.");
        const auto restore = [this] {
            const Keymap defaults(KeyboardLayout::usWindowsFactoryProfile());
            for (std::size_t layer=0; layer<Keymap::layerCount; ++layer)
                for (std::size_t slot=0; slot<Keymap::keysPerLayer; ++slot)
                    keymap_.setScanCode(layer, slot, defaults.scanCode(layer,slot));
        };
        if (ImGui::Button("Restore defaults")) { restore(); finishDialog(); }
        ImGui::SameLine();
        ImGui::BeginDisabled(demo_);
        if (ImGui::Button("Restore and apply")) { restore(); finishDialog(); openApply(); }
        ImGui::EndDisabled();
    }
    if (!filesDialog && !dialogError_.empty()) ImGui::TextWrapped("%s", dialogError_.c_str());
    if (!ownCancel) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancelDialog();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelDialog();
    ImGui::EndPopup();
}

namespace {
constexpr float kPi = 3.14159265f;

// A circular arrow: a three-quarter arc with a head at its end.
void drawRefreshIcon(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color)
{
    const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
    const float r = (max.x - min.x) * .25f;
    const float start = -kPi * .35f, end = kPi * 1.35f;
    draw->PathArcTo(center, r, start, end);
    draw->PathStroke(color, 0, 1.8f);
    const ImVec2 tip(center.x + std::cos(end) * r, center.y + std::sin(end) * r);
    const ImVec2 along(-std::sin(end), std::cos(end)), across(std::cos(end), std::sin(end));
    const float head = r * .75f;
    draw->AddTriangleFilled(ImVec2(tip.x + along.x * head, tip.y + along.y * head),
                            ImVec2(tip.x + across.x * head * .8f, tip.y + across.y * head * .8f),
                            ImVec2(tip.x - across.x * head * .8f, tip.y - across.y * head * .8f), color);
}

// Auto = half-filled disc, Light = sun, Dark = crescent moon.
void drawThemeIcon(ImDrawList* draw, ImVec2 min, ImVec2 max, theme::Mode mode)
{
    const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
    const float r = (max.x - min.x) * .2f;
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    if (mode == theme::Mode::Auto) {
        draw->AddCircle(center, r, color, 0, 1.6f);
        draw->PathArcTo(center, r, -kPi / 2, kPi / 2);
        draw->PathFillConvex(color);
    } else if (mode == theme::Mode::Light) {
        draw->AddCircleFilled(center, r * .7f, color);
        for (int i = 0; i < 8; ++i) {
            const float a = i * kPi / 4;
            const ImVec2 dir(std::cos(a), std::sin(a));
            draw->AddLine(ImVec2(center.x + dir.x * r * 1.15f, center.y + dir.y * r * 1.15f),
                          ImVec2(center.x + dir.x * r * 1.6f, center.y + dir.y * r * 1.6f), color, 1.6f);
        }
    } else {
        draw->AddCircleFilled(center, r * 1.1f, color);
        draw->AddCircleFilled(ImVec2(center.x + r * .6f, center.y - r * .5f), r * .95f,
                              ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive :
                                                 ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
    }
}
}

void MainWindow::draw()
{
    pollScan();
    pollApply();
    const bool busy = this->busy();
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("HHKBS workspace", nullptr, ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    // Title, subtitle and status share one line, with the smaller text sitting on the title's baseline.
    const float titleTop = ImGui::GetCursorPosY();
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextUnformatted("HHKBS");
    ImGui::SetWindowFontScale(1.f);
    const float smallTop = titleTop + ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight() - 3.f;
    ImGui::SameLine(0, 16.f);
    ImGui::SetCursorPosY(smallTop);
    ImGui::TextDisabled("HHKB Studio Keymap Editor for Linux");
    ImGui::SameLine();
    ImGui::SetCursorPosY(smallTop);
    ImGui::Text("  /  %s", status_.c_str());
    // Cycles Auto (follow the desktop) -> Light -> Dark; the icon shows the current mode.
    const float themeSize = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - themeSize);
    ImGui::SetCursorPosY(titleTop);
    if (ImGui::Button("##theme", ImVec2(themeSize, themeSize))) theme::setMode(theme::nextMode(theme::mode()));
    drawThemeIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::mode());
    ImGui::SetItemTooltip("Theme: %s (click to change)", theme::modeName(theme::mode()));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginDisabled(!loaded_ || busy);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Layer");
    const char* layers[] = {"Base", "Fn1", "Fn2", "Fn3"};
    for (std::size_t i=0; i<4; ++i) {
        ImGui::SameLine();
        const bool selected = i == layer_;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().selected);
        if (ImGui::Button(layers[i], ImVec2(76,32))) layer_ = i;
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    // Profiles are right-aligned; wrap below the layers when the window is too narrow.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float refreshWidth = 32.f;
    const float profilesWidth = refreshWidth + spacing + ImGui::CalcTextSize("Keyboard profile").x + 4*(96 + spacing);
    const float profilesX = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - profilesWidth;
    const float layersEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    const bool sideBySide = profilesX >= layersEnd + 36.f;
    if (sideBySide) ImGui::SameLine(profilesX);
    else ImGui::Spacing();
    ImGui::BeginDisabled(demo_ || busy);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keyboard profile");
    for (std::uint16_t i=0; i<4; ++i) {
        ImGui::SameLine();
        const bool selected = selectedProfile_ && *selectedProfile_ == i;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().selected);
        const auto label = "Profile " + std::to_string(i+1);
        if (ImGui::Button(label.c_str(), ImVec2(96,32))) selectProfile(i);
        if (selected) ImGui::PopStyleColor();
    }
    // Re-reads the profile the keyboard is using; it replaces the editor content, so unsaved edits are confirmed first.
    ImGui::SameLine();
    if (ImGui::Button("##refresh", ImVec2(refreshWidth, 32))) request(Action::Read);
    drawRefreshIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SetItemTooltip("Read the profile the keyboard is currently using");
    ImGui::EndDisabled();
    const std::string caption = summary_ + (unsaved() ? (summary_.empty() ? "Unsaved changes" : "  /  Unsaved changes") : "");
    const auto& style = ImGui::GetStyle();
    // Reserve exactly what is drawn under the keyboard: the button row. Messages live inside the keyboard frame.
    const float below = style.ItemSpacing.y + ImGui::GetFrameHeight();
    const float boardHeight = std::max(320.f, ImGui::GetContentRegionAvail().y - below - 2.f);
    ImGui::BeginDisabled(busy);
    if (loaded_) {
        if (const auto slot = drawKeyboard(keymap_, layer_, boardHeight, caption,
                                       message_.empty() && demo_ ? "Demo mode never writes to a keyboard." : message_, message_.empty())) {
            slot_ = *slot;
            assignment_.reset(keymap_.scanCode(layer_,slot_));
            dialog_ = Dialog::Assign;
        }
    } else {
        ImGui::BeginChild("No profile", ImVec2(0,boardHeight), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", busy ? "Looking for an HHKB Studio..." : "Connect a keyboard or import a profile to begin.");
        if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
        ImGui::EndChild();
    }
    // Left: moving data in and out. Right: the editor and the one action that writes to the keyboard.
    if (ImGui::Button("Import")) request(Action::Import);
    ImGui::SameLine();
    ImGui::BeginDisabled(!loaded_);
    if (ImGui::Button("Export")) openFiles(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const auto backupsLabel = "Backups (" + std::to_string(backupCount_) + ")";
    if (ImGui::Button(backupsLabel.c_str())) openBackups();

    const auto buttonWidth = [&](const char* label) { return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2; };
    const float rightWidth = buttonWidth("Restore defaults") + buttonWidth("Discard changes") + buttonWidth("Apply to keyboard") + 2 * style.ItemSpacing.x;
    const float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightWidth;
    if (rightX >= ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 36.f) ImGui::SameLine(rightX);
    else ImGui::SameLine();
    ImGui::BeginDisabled(!loaded_);
    if (ImGui::Button("Restore defaults")) dialog_ = Dialog::Defaults;
    ImGui::SameLine();
    ImGui::BeginDisabled(!keymap_.isModified());
    if (ImGui::Button("Discard changes")) keymap_.reset();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::palette().accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::palette().accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().accentText);
    ImGui::BeginDisabled(!loaded_ || demo_ || busy);
    if (ImGui::Button("Apply to keyboard")) openApply();
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    drawDialog();
    ImGui::End();
}
