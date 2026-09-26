#include "gui/MainWindow.h"
#include "gui/Theme.h"
#include "gui/DialogWidgets.h"
#include "gui/KeyboardWidget.h"
#include "device/DeviceDiscovery.h"
#include "device/HidrawTransport.h"
#include "device/HhkbStudioDevice.h"
#include "keymap/BackupFiles.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ProfileFiles.h"
#include "keymap/ScanCodeCatalog.h"
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

constexpr const char* layerNames[] = {"Base", "Fn1", "Fn2", "Fn3"};

// The legend printed on a key, e.g. "Esc" or "Left side Up".
std::string keyName(std::size_t slot)
{
    for (const auto* keys : {&KeyboardLayout::usStudio(), &KeyboardLayout::gesturePads()})
        for (const auto& key : *keys)
            if (key.slot == slot) return key.legend;
    return "Key " + std::to_string(slot);
}

// With usbOnly, a Bluetooth connection is passed over: writing a profile is only done over the cable.
std::unique_ptr<hhkbs::device::HidrawTransport> openStudio(const bool usbOnly = false)
{
    bool skippedBluetooth = false;
    for (const auto& item : hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces()) {
        if (!item.canReadWrite) continue;
        if (usbOnly && item.bluetooth) { skippedBluetooth = true; continue; }
        try {
            auto transport = std::make_unique<hhkbs::device::HidrawTransport>(item.path);
            if (hhkbs::device::HhkbStudioDevice(*transport).readProductName() == "HHKB-Studio")
                return transport;
        } catch (const std::exception&) {}
    }
    if (skippedBluetooth) throw std::runtime_error("Applying needs a USB connection. Connect the keyboard with a cable.");
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

bool MainWindow::anyUnsaved() const
{
    if (unsaved()) return true;
    for (const auto& stash : stashed_)
        if (stash && stash->keymap.toBytes() != stash->savedBytes) return true;
    return false;
}

// The work on the shown profile, wherever it is kept: the editor for the profile on screen, a stash for the others.
const Keymap* MainWindow::draft(const std::uint16_t profile) const
{
    if (selectedProfile_ == profile) return loaded_ ? &keymap_ : nullptr;
    return stashed_[profile] ? &stashed_[profile]->keymap : nullptr;
}

// The profiles whose work differs from what the keyboard holds, so applying would change something.
std::vector<std::uint16_t> MainWindow::editedProfiles() const
{
    std::vector<std::uint16_t> profiles;
    for (std::uint16_t i = 0; i < 4; ++i)
        if (const auto* keymap = draft(i); keymap && keymap->isModified()) profiles.push_back(i);
    return profiles;
}

void MainWindow::stashShown()
{
    if (selectedProfile_ && loaded_) stashed_[*selectedProfile_] = Stash{keymap_, savedBytes_, summary_};
}

void MainWindow::showStashed(const std::uint16_t profile)
{
    auto stash = std::move(*stashed_[profile]);
    stashed_[profile].reset();
    keymap_ = std::move(stash.keymap);
    savedBytes_ = std::move(stash.savedBytes);
    summary_ = std::move(stash.summary);
    selectedProfile_ = profile;
    loaded_ = true;
    message_.clear();
}

// Content that came from a file or a backup is compared with what the keyboard holds for the shown profile, when that
// has been read, so the keys it would change are marked and Discard changes goes back to the keyboard's content.
void MainWindow::useKeyboardAsReference()
{
    if (selectedProfile_ && !keyboardBytes_[*selectedProfile_].empty()) keymap_.rebase(keyboardBytes_[*selectedProfile_]);
}

void MainWindow::beginScan(std::optional<std::uint16_t> target, const bool reconnect)
{
    if (scan_.valid()) return;
    reconnectScan_ = reconnect;
    if (!reconnect) {
        status_ = "Searching...";
        message_ = "Checking available HID interfaces...";
    }
    scan_ = std::async(std::launch::async, [target, reconnect] {
        ScanResult result{"No device", "Connect an HHKB Studio, import a TOML profile, or start with --demo.", {}, std::nullopt, {}, false, {}};
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
                    if (reconnect) {
                        // Only bring the connection back; the profile is read when the user asks for it.
                        result.path = item.path;
                        result.bluetooth = item.bluetooth;
                        for (std::size_t pad = 0; pad < result.pads.size(); ++pad) {
                            try { result.pads[pad] = device.padState(pad); } catch (const std::exception&) {}
                        }
                        result.status = "Connected";
                        return result;
                    }
                    const auto info = device.readInformation();
                    const auto profile = target.value_or(info.currentProfile);
                    result.bytes = device.readProfile(profile);
                    result.profile = profile;
                    result.path = item.path;
                    result.bluetooth = item.bluetooth;
                    for (std::size_t pad = 0; pad < result.pads.size(); ++pad) {
                        try { result.pads[pad] = device.padState(pad); } catch (const std::exception&) {}
                    }
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
        if (reconnectScan_) {
            // A probe that finds nothing keeps trying; it only speaks up when the keyboard is there but unusable.
            if (result.path.empty()) {
                if (result.status != "No device") { status_ = result.status; message_ = result.detail; }
                return;
            }
            // The editor content is left alone, edited or not.
            disconnected_ = false;
            bluetooth_ = result.bluetooth;
            status_ = result.status;
            message_.clear();
            pads_.start(result.path);
            wasListening_ = true;
            for (std::size_t pad = 0; pad < result.pads.size(); ++pad)
                if (result.pads[pad]) pads_.set(pad, *result.pads[pad]);
            return;
        }
        status_ = result.status;
        message_ = result.detail;
        if (!result.bytes.empty()) {
            const auto profile = result.profile.value_or(0);
            keyboardBytes_[profile] = result.bytes;
            Keymap fresh(result.bytes);
            if (selectedProfile_ != profile) {
                // Another profile comes onto the screen: the one that was there is kept, and this one continues where
                // it was left, with edits it may have. Only the keyboard's content is brought up to date.
                stashShown();
                if (stashed_[profile]) showStashed(profile);
                if (selectedProfile_ == profile && keymap_.isModified()) keymap_.rebase(result.bytes);
                else keymap_ = std::move(fresh);
            } else keymap_ = std::move(fresh);
            if (!keymap_.isModified()) savedBytes_ = keymap_.toBytes();
            loaded_ = true;
            selectedProfile_ = profile;
            disconnected_ = false;
            if (!result.path.empty()) {
                pads_.start(result.path);
                wasListening_ = true;
                for (std::size_t pad = 0; pad < result.pads.size(); ++pad)
                    if (result.pads[pad]) pads_.set(pad, *result.pads[pad]);
            }
            summary_ = result.detail;
            message_.clear();
        }
    } catch (const std::exception& error) { status_ = "Profile error"; message_ = error.what(); }
}

void MainWindow::beginPadChange(const std::size_t pad, const bool on)
{
    if (busy() || demo_) return;
    status_ = "Switching pad...";
    message_.clear();
    pad_ = std::async(std::launch::async, [pad, on] {
        PadResult result{false, pad, on, {}};
        try {
            auto transport = openStudio();
            hhkbs::device::HhkbStudioDevice(*transport).setPadState(pad, on);
            result.ok = true;
        } catch (const std::exception& error) { result.message = error.what(); }
        return result;
    });
}

void MainWindow::pollPadChange()
{
    if (!pad_.valid() || pad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = pad_.get();
    if (result.ok) {
        pads_.set(result.pad, result.on);
        status_ = "Connected";
    } else {
        status_ = "Pad change failed";
        message_ = result.message;
    }
}

void MainWindow::pollConnection()
{
    // Writes reopen the interface and may briefly disturb the listener, so only judge while nothing is running.
    if (busy()) return;
    if (disconnected_) {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextProbe_) return;
        nextProbe_ = now + std::chrono::seconds(1);
        // Looking at the interfaces costs the keyboard nothing. A freshly connected keyboard is left alone until
        // they have stopped changing for a few seconds; talking to it while it is still coming up can freeze it.
        std::vector<std::filesystem::path> paths;
        for (const auto& item : hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces()) paths.push_back(item.path);
        if (paths != seenPaths_) { seenPaths_ = paths; settledAt_ = now + std::chrono::seconds(3); }
        if (paths.empty() || now < settledAt_) return;
        nextProbe_ = now + std::chrono::seconds(2);
        beginScan(selectedProfile_, true);
        return;
    }
    if (!wasListening_ || pads_.listening()) return;
    wasListening_ = false;
    disconnected_ = true;
    seenPaths_.clear();
    nextProbe_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    status_ = "Disconnected";
    message_ = "The keyboard connection was lost. It reconnects by itself; press the refresh button to read the profile again.";
}

void MainWindow::beginApply()
{
    if (busy() || demo_ || !loaded_) return;
    std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> jobs;
    for (std::uint16_t i = 0; i < 4; ++i)
        if (applyPick_[i])
            if (const auto* keymap = draft(i)) jobs.emplace_back(i, keymap->toBytes());
    if (jobs.empty()) return;
    status_ = "Applying...";
    message_ = "Writing to the keyboard. Do not unplug it.";
    apply_ = std::async(std::launch::async, [jobs = std::move(jobs)] {
        ApplyResult result;
        const auto listed = [](const std::vector<std::uint16_t>& profiles) {
            std::string text = profiles.size() == 1 ? "profile " : "profiles ";
            for (std::size_t i = 0; i < profiles.size(); ++i) text += (i ? ", " : "") + std::to_string(profiles[i] + 1);
            return text;
        };
        std::string backups;
        try {
            auto transport = openStudio(true);
            hhkbs::device::HhkbStudioDevice device(*transport);
            for (const auto& [profile, bytes] : jobs) {
                std::filesystem::path path;
                try {
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
                } catch (const std::exception& error) {
                    throw std::runtime_error("Profile " + std::to_string(profile + 1) + " was not written: " + error.what());
                }
                result.written.push_back(profile);
                result.bytes[profile] = bytes;
                backups += (backups.empty() ? "" : ", ") + path.filename().string();
            }
            result.ok = true;
            result.message = "Applied to " + listed(result.written) + ". The previous content was saved as " + backups + ".";
        } catch (const std::exception& error) {
            result.message = error.what();
            if (!result.written.empty()) result.message += " Already written: " + listed(result.written) + ".";
        }
        return result;
    });
}

void MainWindow::pollApply()
{
    if (!apply_.valid() || apply_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        const auto result = apply_.get();
        message_ = result.message;
        // A written profile now holds its work, so it stops counting as changed.
        for (const auto profile : result.written) {
            keyboardBytes_[profile] = result.bytes[profile];
            if (selectedProfile_ == profile) {
                keymap_.rebase(result.bytes[profile]);
                savedBytes_ = keymap_.toBytes();
            } else if (stashed_[profile]) {
                stashed_[profile]->keymap.rebase(result.bytes[profile]);
                stashed_[profile]->savedBytes = stashed_[profile]->keymap.toBytes();
            }
        }
        if (!result.written.empty()) refreshBackupCount();
        status_ = result.ok ? "Applied" : "Apply failed";
    } catch (const std::exception& error) { status_ = "Apply failed"; message_ = error.what(); }
}

// Reads the profiles that have changes, so the Apply dialog can list what would change on each. Writing is USB only,
// so there is nothing to preview over Bluetooth.
void MainWindow::beginPreview()
{
    if (busy() || demo_ || bluetooth_) return;
    const auto profiles = editedProfiles();
    if (profiles.empty()) { previewDone_ = true; return; }
    preview_ = std::async(std::launch::async, [profiles] {
        PreviewResult result;
        std::unique_ptr<hhkbs::device::HidrawTransport> transport;
        std::string failure;
        try { transport = openStudio(true); } catch (const std::exception& error) { failure = error.what(); }
        for (const auto profile : profiles) {
            ProfileRead read{profile, {}, failure};
            if (failure.empty()) {
                try { read.bytes = hhkbs::device::HhkbStudioDevice(*transport).readProfile(profile); }
                catch (const std::exception& error) { read.error = error.what(); }
            }
            result.profiles.push_back(std::move(read));
        }
        return result;
    });
}

void MainWindow::pollPreview()
{
    if (!preview_.valid() || preview_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = preview_.get();
    previews_ = {};
    for (const auto& read : result.profiles) {
        auto& preview = previews_[read.profile];
        preview.read = true;
        preview.error = read.error;
        if (!read.error.empty()) continue;
        try {
            if (const auto* keymap = draft(read.profile))
                preview.changes = hhkbs::keymap::diffProfiles(Keymap(read.bytes).layers(), keymap->layers());
        } catch (const std::exception& error) { preview.error = error.what(); }
    }
    previewDone_ = true;
}

void MainWindow::requestClose()
{
    if (busy()) { message_ = "Please wait for the keyboard operation to finish before closing."; return; }
    if (dialog_ == Dialog::None) request(Action::Close);
}
// Each profile keeps its own work: leaving one for another puts it aside and shows the other's, which is read from
// the keyboard only the first time.
void MainWindow::selectProfile(std::uint16_t profile)
{
    if (busy() || demo_ || profile == selectedProfile_) return;
    if (stashed_[profile]) {
        stashShown();
        showStashed(profile);
        return;
    }
    beginScan(profile);
}
void MainWindow::request(Action action)
{
    // Closing loses the work on every profile; the other actions only replace the one on screen.
    if (action == Action::Close ? anyUnsaved() : unsaved()) { pending_ = action; dialog_ = Dialog::Unsaved; }
    else perform(action);
}
void MainWindow::perform(Action action)
{
    if (action == Action::Read) beginScan();
    else if (action == Action::Import) openFiles(false);
    else if (action == Action::LoadBackup && pendingBackup_) {
        const auto entry = *pendingBackup_;
        pendingBackup_.reset();
        if (!loadBackup(entry)) message_ = dialogError_;
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
// The profiles with changes are listed and picked; from "Restore and apply" of the defaults only the shown profile is, since that is
// the one the restored content is for.
void MainWindow::openApply(const bool onlyShown)
{
    const auto edited = editedProfiles();
    applyPick_.fill(false);
    for (const auto profile : edited) applyPick_[profile] = !onlyShown || selectedProfile_ == profile;
    applyView_ = edited.empty() ? 0 : edited.front();
    if (selectedProfile_ && std::find(edited.begin(), edited.end(), *selectedProfile_) != edited.end()) applyView_ = *selectedProfile_;
    previewDone_ = false;
    previews_ = {};
    dialogError_.clear();
    dialog_ = Dialog::Apply;
}
void MainWindow::openBackups(bool manage)
{
    backups_ = hhkbs::keymap::listBackups(hhkbs::keymap::backupDirectory());
    backupChoice_.reset();
    tagShownFor_.reset();
    confirmDelete_ = false;
    backupCount_ = backups_.size();
    dialogError_.clear();
    selectManageTab_ = manage;
    dialog_ = Dialog::Backups;
}
void MainWindow::requestLoadBackup(const hhkbs::keymap::BackupEntry& entry)
{
    // Loading replaces the editor, so unsaved edits get the usual chance to be exported first.
    pendingBackup_ = entry;
    request(Action::LoadBackup);
}
bool MainWindow::loadBackup(const hhkbs::keymap::BackupEntry& entry)
{
    try {
        auto profile = hhkbs::keymap::readProfile(entry.path);
        keymap_ = std::move(profile);
        useKeyboardAsReference();
        savedBytes_ = keymap_.toBytes();
        loaded_ = true;
        summary_ = std::string("Backup ") + (entry.tag.empty() ? "" : "\"" + entry.tag + "\" ") + "of Profile " +
                   std::to_string(entry.profile + 1) + " from " + entry.timestamp;
        status_ = "Loaded backup";
        message_.clear();
        finishDialog();
        return true;
    } catch (const std::exception& error) { dialogError_ = error.what(); }
    return false;
}
void MainWindow::openBackupFolder()
{
    if (!commandExists("xdg-open"))
        dialogError_ = "xdg-open was not found. Open " + hhkbs::keymap::backupDirectory().string() + " yourself.";
    else if (!openFolder(hhkbs::keymap::backupDirectory())) dialogError_ = "Could not open the folder.";
    else dialogError_.clear();
}
// The list fills the dialog down to what sits under it (`belowList`), and an error message under the list takes its
// own height from the list. So nothing under the list ever floats away from it, whatever the dialog shows.
void MainWindow::drawBackupList(const float belowList)
{
    const auto folder = hhkbs::keymap::backupDirectory();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", folder.c_str());
    // The button that opens the folder sits on the line that names it.
    const auto& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize("Open folder").x + style.FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - width);
    ImGui::BeginDisabled(!std::filesystem::is_directory(folder));
    if (ImGui::Button("Open folder")) openBackupFolder();
    ImGui::EndDisabled();
    const float errorHeight = dialogError_.empty() ? 0.f
        : ImGui::CalcTextSize(dialogError_.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + style.ItemSpacing.y;
    ImGui::BeginChild("Backups", ImVec2(0, -(belowList + errorHeight)), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (backups_.empty()) ImGui::TextDisabled("No backups yet. One is saved before every apply.");
    // A table: the date it was saved (dimmed), the tag (bright, blank when there is none), and on the right the
    // profile it came from, drawn as a small pill.
    const float pillPadding = 9.f;
    const float pillInset = 4.f;  // keeps the pill's outline inside the cell, which clips at its edge
    const float pillWidth = ImGui::CalcTextSize("Profile 4").x + pillPadding * 2;
    if (!backups_.empty() && ImGui::BeginTable("BackupRows", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000-00-00 00:00:00").x + 24.f);
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthFixed, pillWidth + 16.f);
        ImGui::TableSetupScrollFreeze(0, 1);
        const auto rightAligned = [](float width) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - width));
        };
        // Each header is centered over what its column holds: the date, the whole tag column, the pill.
        const auto header = [](const char* text, float over, float offset) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset + std::max(0.f, (over - ImGui::CalcTextSize(text).x) / 2));
            ImGui::TextDisabled("%s", text);
        };
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        header("Saved", ImGui::CalcTextSize("0000-00-00 00:00:00").x, 0.f);
        ImGui::TableSetColumnIndex(1);
        header("Tag", ImGui::GetContentRegionAvail().x, 0.f);
        ImGui::TableSetColumnIndex(2);
        header("Profile", pillWidth, std::max(0.f, ImGui::GetContentRegionAvail().x - pillWidth - pillInset));
        for (std::size_t i=0; i<backups_.size(); ++i) {
            const auto& entry = backups_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(entry.timestamp.c_str(), backupChoice_ == i, ImGuiSelectableFlags_SpanAllColumns)) backupChoice_ = i;
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.tag.c_str());
            ImGui::TableSetColumnIndex(2);
            const auto profile = "Profile " + std::to_string(entry.profile + 1);
            const ImVec2 size(pillWidth, ImGui::GetTextLineHeight() + 2.f);
            rightAligned(size.x + pillInset);
            const ImVec2 top = ImGui::GetCursorScreenPos(), bottom(top.x + size.x, top.y + size.y);
            const auto& palette = theme::palette();
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(top, bottom, palette.keyFill, size.y / 2);
            draw->AddRect(top, bottom, palette.keyBorder, size.y / 2);
            const float textWidth = ImGui::CalcTextSize(profile.c_str()).x;
            draw->AddText(ImVec2(top.x + (size.x - textWidth) / 2, top.y + 1.f), ImGui::GetColorU32(ImGuiCol_Text), profile.c_str());
            ImGui::Dummy(size);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
void MainWindow::drawBackups()
{
    dialog::title("Backups", "A backup is saved before every apply.");
    if (!ImGui::BeginTabBar("BackupTabs")) return;
    const bool chosen = backupChoice_.has_value();
    // What is under the list: the row of buttons, and in the Manage tab the tag row too (plus the gaps between them).
    const auto& style = ImGui::GetStyle();
    const float footerBelow = style.ItemSpacing.y * 2 + ImGui::GetFrameHeight() + 2.f;
    const float tagRowBelow = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    if (ImGui::BeginTabItem("Restore")) {
        dialog::hint("Load a backup into the editor. It is written to the keyboard only when you apply it there.");
        drawBackupList(footerBelow);
        dialog::error(dialogError_);
        dialog::pinFooter();
        // The actions start at the left edge, Close is always at the right edge, on both tabs.
        const int hit = dialog::footer({{"Load into editor", true, false, chosen}}, {{"Close"}});
        if (hit == 0) requestLoadBackup(backups_[*backupChoice_]);
        else if (hit == 1) cancelDialog();
        ImGui::EndTabItem();
    }
    // Coming back from a delete or clean-up lands on the tab the user left.
    const auto flags = selectManageTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    selectManageTab_ = false;
    if (ImGui::BeginTabItem("Manage", nullptr, flags)) {
        dialog::hint("Tag or delete backups. HHKBS never deletes them on its own.");
        drawBackupList(footerBelow + tagRowBelow);
        // The tag box follows the chosen backup and starts from its current tag; saving it blank removes the tag.
        if (!chosen) { tagInput_[0] = '\0'; tagShownFor_.reset(); }
        else if (tagShownFor_ != backupChoice_) {
            std::snprintf(tagInput_.data(), tagInput_.size(), "%s", backups_[*backupChoice_].tag.c_str());
            tagShownFor_ = backupChoice_;
        }
        // Everything that acts on the chosen backup is on this one line: its tag, and deleting it.
        ImGui::BeginDisabled(!chosen);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tag");
        ImGui::SameLine();
        const auto& style = ImGui::GetStyle();
        const auto buttonWidth = [&](const char* label) { return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2; };
        // Save tag belongs to the input, so it sits close to it; Delete keeps the usual gap so it is not hit by mistake.
        const float tagGap = 4.f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth("Save tag") - buttonWidth("Delete") - tagGap - style.ItemSpacing.x);
        const bool entered = ImGui::InputText("##tag", tagInput_.data(), tagInput_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0, tagGap);
        const bool saveClicked = ImGui::Button("Save tag");
        ImGui::SameLine();
        const auto& palette = theme::palette();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.78f, .22f, .22f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.85f, .28f, .28f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.68f, .18f, .18f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, palette.accentText);
        const bool deleteClicked = ImGui::Button("Delete");
        ImGui::PopStyleColor(4);
        ImGui::EndDisabled();
        if (chosen && (entered || saveClicked)) saveBackupTag();
        if (chosen && deleteClicked) { confirmDelete_ = true; ImGui::OpenPopup("Delete backup"); }
        dialog::error(dialogError_);
        dialog::pinFooter();
        const int hit = dialog::footer({{"Clean up...", false, false, !backups_.empty()}}, {{"Close"}});
        if (hit == 0) dialog_ = Dialog::CleanBackups;
        else if (hit == 1) cancelDialog();
        drawDeleteBackup();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
void MainWindow::saveBackupTag()
{
    if (!backupChoice_) return;
    const auto path = backups_[*backupChoice_].path;
    try {
        hhkbs::keymap::setBackupTag(hhkbs::keymap::backupDirectory(), backups_[*backupChoice_], tagInput_.data());
    } catch (const std::exception& error) { dialogError_ = error.what(); return; }
    dialogError_.clear();
    // Reload so the list shows the new tag, and keep the same backup chosen.
    backups_ = hhkbs::keymap::listBackups(hhkbs::keymap::backupDirectory());
    backupChoice_.reset();
    tagShownFor_.reset();
    for (std::size_t i = 0; i < backups_.size(); ++i)
        if (backups_[i].path == path) backupChoice_ = i;
}
void MainWindow::drawCleanBackups()
{
    dialog::title("Clean up backups");
    dialog::hint("Keep the newest backups of each profile and delete the rest. Tagged ones are kept.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keep the newest");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputInt("##keep", &keepBackups_);
    keepBackups_ = std::clamp(keepBackups_, 1, 999);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("backups of each profile");
    const auto surplus = hhkbs::keymap::backupsBeyondNewest(backups_, static_cast<std::size_t>(keepBackups_));
    ImGui::TextWrapped("%zu of %zu backups will be deleted.", surplus.size(), backups_.size());
    dialog::error(dialogError_);
    dialog::pinFooter();
    const int hit = dialog::footer({{"Back"}, {"Delete backups", false, true, !surplus.empty()}});
    if (hit == 0) { dialog_ = Dialog::Backups; selectManageTab_ = true; }
    else if (hit == 1) {
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
}
// The confirmation opens as its own small popup over the Backups window, which stays as it is behind it.
void MainWindow::drawDeleteBackup()
{
    if (!confirmDelete_ || !backupChoice_) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(460,0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Delete backup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto entry = backups_[*backupChoice_];
    dialog::title("Delete backup");
    const std::string named = entry.tag.empty() ? "" : "\"" + entry.tag + "\" ";
    ImGui::TextWrapped("Delete the backup %sof Profile %d saved on %s? This cannot be undone.", named.c_str(), entry.profile + 1, entry.timestamp.c_str());
    const int hit = dialog::footer({{"Back"}, {"Delete backup", false, true}});
    if (hit == 0) { confirmDelete_ = false; ImGui::CloseCurrentPopup(); }
    else if (hit == 1) {
        confirmDelete_ = false;
        ImGui::CloseCurrentPopup();
        try {
            hhkbs::keymap::deleteBackup(hhkbs::keymap::backupDirectory(), entry);
            openBackups(true);
        } catch (const std::exception& error) { dialogError_ = error.what(); }  // shown under the list
    }
    ImGui::EndPopup();
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
    dialog::title(save ? "Export TOML profile" : "Import TOML profile");

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
    dialog::error(dialogError_);
    const bool ready = save ? fileName_[0] != '\0' : (path_[0] != '\0' || !importPath_.empty());
    const int hit = dialog::footer({{"Cancel"}, {save ? "Export" : "Import", true, false, ready}});
    if (hit == 0) { cancelDialog(); return; }
    const bool pressed = hit == 1;
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
            useKeyboardAsReference();
            savedBytes_ = keymap_.toBytes();
            loaded_ = true;
            summary_ = "Imported " + target.filename().string();
            status_ = "Imported profile";
            message_.clear();
            directory_ = target.parent_path();
            finishDialog();
        }
    } catch (const std::exception& error) { importPath_.clear(); dialogError_ = error.what(); }
}

// The keys the Apply would change on the listed profile: where it is, what the keyboard has now, what it gets.
void MainWindow::drawChanges()
{
    if (demo_ || bluetooth_) return;
    const auto& style = ImGui::GetStyle();
    const auto& preview = previews_[applyView_];
    const int number = applyView_ + 1;
    if (preview_.valid() || !preview.read) {
        ImGui::TextDisabled("Reading the keyboard's Profile %d...", number);
        return;
    }
    if (!preview.error.empty()) {
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextDisabled("Could not read the keyboard to compare: %s", preview.error.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    if (preview.changes.empty()) {
        ImGui::TextDisabled("No key differs from the keyboard's Profile %d.", number);
        return;
    }
    ImGui::Text("Profile %d: %zu key%s will change", number, preview.changes.size(), preview.changes.size() == 1 ? "" : "s");
    // As tall as the list needs, up to ten rows, and never past the bottom of the window: a longer list scrolls in
    // the table. Under the table come the gap, an error line if there is one, and the footer's spacing, buttons and
    // the window's bottom padding.
    const float rowHeight = ImGui::GetTextLineHeight() + style.CellPadding.y * 2;
    const float error = dialogError_.empty() ? 0.f
        : ImGui::CalcTextSize(dialogError_.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + style.ItemSpacing.y;
    const float footer = style.ItemSpacing.y * 3 + error + ImGui::GetFrameHeight() + style.WindowPadding.y + 2.f;
    const auto* viewport = ImGui::GetMainViewport();
    // The dialog stays centered and may be as tall as the window less a margin; the rest of that is for the table.
    const float room = viewport->WorkSize.y - 40.f - ImGui::GetCursorPosY() - footer;
    const float fitting = std::max(3.f, std::floor(room / rowHeight) - 1.f);  // one row is the header
    const float rows = std::min({static_cast<float>(preview.changes.size()), 10.f, fitting});
    if (!ImGui::BeginTable("ApplyChanges", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders,
                           ImVec2(0, rowHeight * (rows + 1)))) return;
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.f);
    ImGui::TableSetupColumn("On the keyboard", ImGuiTableColumnFlags_WidthStretch, 1.f);
    ImGui::TableSetupColumn("After apply", ImGuiTableColumnFlags_WidthStretch, 1.f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const auto& change : preview.changes) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s  %s", layerNames[change.layer], keyName(change.slot).c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", hhkbs::keymap::ScanCodeCatalog::labelFor(change.before).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(hhkbs::keymap::ScanCodeCatalog::labelFor(change.after).c_str());
    }
    ImGui::EndTable();
}

void MainWindow::drawDialog()
{
    if (dialog_ == Dialog::None) return;
    if (!ImGui::IsPopupOpen("HHKBS")) ImGui::OpenPopup("HHKBS");
    const auto* viewport = ImGui::GetMainViewport();
    // The Backups dialogs share one height, in lines of text so it follows the font, and never taller than the window;
    // every other dialog is as tall as its content.
    const bool fixedHeight = dialog_ == Dialog::Backups || dialog_ == Dialog::CleanBackups;
    const float backupsHeight = std::min(ImGui::GetFrameHeightWithSpacing() * 14.f, viewport->WorkSize.y - 40.f);
    // The Apply dialog grows when the keyboard's answer brings the list of changes, so it is kept centered instead of
    // only when it appears; otherwise it would grow down and off the window.
    ImGui::SetNextWindowPos(viewport->GetCenter(), dialog_ == Dialog::Apply ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(620, fixedHeight ? backupsHeight : 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("HHKBS", nullptr, fixedHeight ? ImGuiWindowFlags_NoResize : ImGuiWindowFlags_AlwaysAutoResize)) return;
    // Every dialog draws its own title, error line and footer through dialog::.
    if (dialog_ == Dialog::Assign) {
        bool cancelled = false;
        if (const auto code = assignment_.draw(cancelled)) { keymap_.setScanCode(layer_, slot_, *code); finishDialog(); }
        else if (cancelled) cancelDialog();
    } else if (dialog_ == Dialog::Unsaved) {
        dialog::title("Unsaved keymap changes");
        ImGui::TextWrapped("Export the modified profile before continuing?");
        dialog::error(dialogError_);
        const int hit = dialog::footer({{"Cancel"}, {"Discard and continue"}, {"Export first", true}});
        if (hit == 0) cancelDialog();
        else if (hit == 1) {
            const auto action = std::exchange(pending_, Action::None);
            finishDialog();
            perform(action);
        } else if (hit == 2) openFiles(true);
    } else if (dialog_ == Dialog::Import || dialog_ == Dialog::Export) drawFiles();
    else if (dialog_ == Dialog::Backups) drawBackups();
    else if (dialog_ == Dialog::CleanBackups) drawCleanBackups();
    else if (dialog_ == Dialog::Overwrite) {
        dialog::title("Replace existing file?");
        ImGui::TextWrapped("%s", path_.data());
        dialog::error(dialogError_);
        const int hit = dialog::footer({{"Back"}, {"Replace file", false, true}});
        if (hit == 0) dialog_ = Dialog::Export;
        else if (hit == 1) saveFile(true);
    } else if (dialog_ == Dialog::Apply) {
        dialog::title("Apply to keyboard");
        const auto edited = editedProfiles();
        bool anyPicked = false;
        if (edited.empty()) dialog::hint("No profile has changes to write.");
        else {
            dialog::hint("Profiles to write. Each one is written with its own work.");
            if (!previewDone_ && !preview_.valid()) beginPreview();
            for (const auto profile : edited) {
                ImGui::PushID(profile);
                ImGui::Checkbox("##pick", &applyPick_[profile]);
                ImGui::SameLine();
                std::string label = "Profile " + std::to_string(profile + 1);
                const auto& preview = previews_[profile];
                if (preview.read && preview.error.empty())
                    label += "   " + std::to_string(preview.changes.size()) + (preview.changes.size() == 1 ? " key" : " keys");
                if (ImGui::Selectable(label.c_str(), applyView_ == profile)) applyView_ = profile;
                ImGui::PopID();
                anyPicked = anyPicked || applyPick_[profile];
            }
            ImGui::TextWrapped("The keyboard's current content of each is saved as a backup first, and the result is read back to verify it. "
                               "The keyboard returns to the profile it was on afterwards. "
                               "Do not unplug the keyboard while writing.");
            drawChanges();
        }
        dialog::error(dialogError_);
        const int hit = dialog::footer({{"Cancel"}, {"Apply", true, false, anyPicked && !preview_.valid()}});
        if (hit == 0) cancelDialog();
        else if (hit == 1) { finishDialog(); beginApply(); }
    } else if (dialog_ == Dialog::Defaults) {
        dialog::title("Restore defaults");
        ImGui::TextWrapped("Restore the US Profile 1 defaults in the editor? "
                           "Restoring alone does not change the keyboard; use \"Restore and apply\" to write them right away.");
        const auto restore = [this] {
            const Keymap defaults(KeyboardLayout::usWindowsFactoryProfile());
            for (std::size_t layer=0; layer<Keymap::layerCount; ++layer)
                for (std::size_t slot=0; slot<Keymap::keysPerLayer; ++slot)
                    keymap_.setScanCode(layer, slot, defaults.scanCode(layer,slot));
        };
        dialog::error(dialogError_);
        const int hit = dialog::footer({{"Cancel"}, {"Restore and apply", false, false, !demo_}, {"Restore defaults", true}});
        if (hit == 0) cancelDialog();
        else if (hit == 1) { restore(); finishDialog(); openApply(true); }
        else if (hit == 2) { restore(); finishDialog(); }
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
    const float r = (max.x - min.x) * .19f;
    const float start = -kPi * .35f, end = kPi * 1.35f;
    draw->PathArcTo(center, r, start, end);
    draw->PathStroke(color, 0, 1.6f);
    const ImVec2 tip(center.x + std::cos(end) * r, center.y + std::sin(end) * r);
    const ImVec2 along(-std::sin(end), std::cos(end)), across(std::cos(end), std::sin(end));
    const float head = r * .7f;
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
    pollPreview();
    pollPadChange();
    pollConnection();
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
    const auto editedProfiles = this->editedProfiles();
    for (std::uint16_t i=0; i<4; ++i) {
        ImGui::SameLine();
        const bool selected = selectedProfile_ && *selectedProfile_ == i;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, theme::palette().selected);
        const auto label = "Profile " + std::to_string(i+1);
        // Over Bluetooth only the profile the keyboard is on can be read; one already read can still be shown.
        const bool usbOnly = bluetooth_ && !selected && !stashed_[i];
        ImGui::BeginDisabled(usbOnly);
        if (ImGui::Button(label.c_str(), ImVec2(96,32))) selectProfile(i);
        ImGui::EndDisabled();
        // A dot marks a profile whose work differs from what the keyboard holds.
        if (std::find(editedProfiles.begin(), editedProfiles.end(), i) != editedProfiles.end())
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImGui::GetItemRectMax().x - 9.f, ImGui::GetItemRectMin().y + 9.f),
                                                        3.f, theme::palette().keyBorderChanged);
        if (usbOnly) ImGui::SetItemTooltip("Other profiles can be read over USB");
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
    std::array<std::optional<bool>, 4> padStates{};
    for (std::size_t pad = 0; pad < padStates.size(); ++pad) {
        const auto state = pads_.state(pad);
        if (state != hhkbs::device::PadMonitor::State::Unknown) padStates[pad] = state == hhkbs::device::PadMonitor::State::On;
    }
    ImGui::BeginDisabled(busy);
    if (loaded_) {
        std::optional<std::size_t> padToggled;
        const auto slot = drawKeyboard(keymap_, layer_, boardHeight, padStates, padToggled, caption,
                                       message_.empty() && demo_ ? "Demo mode: nothing is written." : message_, message_.empty());
        if (padToggled) beginPadChange(*padToggled, !padStates[*padToggled].value_or(true));
        if (slot) {
            slot_ = *slot;
            assignment_.reset(keymap_.scanCode(layer_,slot_), keyName(slot_) + " (" + layerNames[layer_] + ")");
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
    const bool nothingToApply = editedProfiles.empty();
    ImGui::BeginDisabled(!loaded_ || demo_ || busy || bluetooth_ || nothingToApply);
    if (ImGui::Button("Apply to keyboard")) openApply();
    ImGui::EndDisabled();
    if (bluetooth_) ImGui::SetItemTooltip("Applying needs a USB connection");
    else if (nothingToApply && loaded_ && !demo_) ImGui::SetItemTooltip("No profile has changes to write");
    ImGui::PopStyleColor(4);
    drawDialog();
    ImGui::End();
}
