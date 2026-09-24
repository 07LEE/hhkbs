#include "gui/MainWindow.h"
#include "gui/KeyboardWidget.h"
#include "device/DeviceDiscovery.h"
#include "device/HidrawTransport.h"
#include "device/HhkbStudioDevice.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ProfileFiles.h"
#include "keymap/ProfileSerializer.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

using hhkbs::keymap::Keymap;
using hhkbs::keymap::KeyboardLayout;

namespace {

std::filesystem::path backupDirectory()
{
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
        return std::filesystem::path(state) / "hhkbs" / "backups";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local" / "state" / "hhkbs" / "backups";
    return std::filesystem::temp_directory_path() / "hhkbs-backups";
}

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
    if (busy() || demo_ || !loaded_) return;
    status_ = "Applying...";
    message_ = "Writing the profile to the keyboard. Do not unplug it.";
    auto bytes = keymap_.toBytes();
    const auto target = selectedProfile_;
    apply_ = std::async(std::launch::async, [bytes = std::move(bytes), target] {
        ApplyResult result{false, {}, bytes, 0};
        try {
            auto transport = openStudio();
            hhkbs::device::HhkbStudioDevice device(*transport);
            const auto profile = target.value_or(device.readInformation().currentProfile);
            std::filesystem::path path;
            device.runOnProfile(profile, [&] {
                device.requireTarget(profile);
                const auto backup = device.readCurrentProfile();

                // Keep a copy of what the keyboard held; without it a failed write cannot be undone by hand.
                const auto directory = backupDirectory();
                std::filesystem::create_directories(directory);
                char stamp[32]{};
                const std::time_t now = std::time(nullptr);
                std::tm local{};
                localtime_r(&now, &local);
                std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
                path = directory / ("backup-" + std::string(stamp) + "-profile" + std::to_string(profile + 1) + ".toml");
                hhkbs::keymap::writeProfile(path, hhkbs::keymap::Keymap(backup), false);

                device.writeCurrentProfile(bytes, backup);
            });
            result.ok = true;
            result.profile = profile;
            result.message = "Applied to profile " + std::to_string(profile + 1) + ". Previous profile saved to " + path.string();
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
    else if (action == Action::Close) close_ = true;
}
void MainWindow::openFiles(bool save)
{
    dialog_ = save ? Dialog::Export : Dialog::Import;
    dialogError_.clear();
    std::snprintf(path_.data(), path_.size(), "%s", (directory_ / (save ? "profile.toml" : "")).c_str());
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

void MainWindow::drawFiles()
{
    const bool save = dialog_ == Dialog::Export;
    ImGui::TextUnformatted(save ? "Export TOML profile" : "Import TOML profile");
    ImGui::TextWrapped("Folder: %s", directory_.c_str());
    if (ImGui::Button("Parent folder")) directory_ = directory_.parent_path().empty() ? "/" : directory_.parent_path();
    ImGui::SameLine();
    if (ImGui::Button("Open path folder")) {
        std::error_code ec;
        auto entered = std::filesystem::path(path_.data());
        if (std::filesystem::is_directory(entered, ec)) directory_ = entered;
        else if (std::filesystem::is_directory(entered.parent_path(), ec)) directory_ = entered.parent_path();
        else dialogError_ = "That folder is unavailable.";
    }
    ImGui::BeginChild("Files", ImVec2(0, 250), ImGuiChildFlags_Borders);
    struct Entry { std::filesystem::path path; bool directory; };
    std::vector<Entry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it(directory_, error), end;
    while (!error && it != end) {
        std::error_code ec;
        const bool isDirectory = it->is_directory(ec);
        if (!ec && (isDirectory || it->path().extension() == ".toml")) entries.push_back({it->path(), isDirectory});
        it.increment(error);
    }
    if (error) ImGui::TextWrapped("Cannot list folder: %s", error.message().c_str());
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.directory != b.directory ? a.directory > b.directory : a.path.filename() < b.path.filename();
    });
    for (std::size_t i=0; i<entries.size(); ++i) {
        const auto& entry = entries[i];
        const auto name = (entry.directory ? "[Folder] " : "") + entry.path.filename().string();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(name.c_str())) {
            if (entry.directory) directory_ = entry.path;
            else std::snprintf(path_.data(), path_.size(), "%s", entry.path.c_str());
            dialogError_.clear();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##path", path_.data(), path_.size());
    if (ImGui::Button(save ? "Export" : "Import", ImVec2(110,0))) {
        try {
            auto target = std::filesystem::path(path_.data());
            if (target.empty()) throw std::runtime_error("Choose a file.");
            if (save) {
                if (target.extension().empty()) target += ".toml";
                std::snprintf(path_.data(), path_.size(), "%s", target.c_str());
                std::error_code ec;
                if (std::filesystem::exists(std::filesystem::symlink_status(target, ec))) dialog_ = Dialog::Overwrite;
                else saveFile(false);
            } else {
                auto profile = hhkbs::keymap::readProfile(target);
                keymap_ = std::move(profile);
                savedBytes_ = keymap_.toBytes();
                loaded_ = true;
                summary_ = "Imported " + target.filename().string();
                status_ = "Imported profile";
                message_.clear();
                directory_ = target.parent_path();
                finishDialog();
            }
        } catch (const std::exception& error) { dialogError_ = error.what(); }
    }
}

void MainWindow::drawDialog()
{
    if (dialog_ == Dialog::None) return;
    if (!ImGui::IsPopupOpen("HHKBS")) ImGui::OpenPopup("HHKBS");
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(620,0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("HHKBS", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
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
    else if (dialog_ == Dialog::Overwrite) {
        ImGui::TextWrapped("Replace the existing file?\n%s", path_.data());
        if (ImGui::Button("Replace file")) saveFile(true);
        ImGui::SameLine();
        if (ImGui::Button("Back")) dialog_ = Dialog::Export;
    } else if (dialog_ == Dialog::Apply) {
        ImGui::TextUnformatted("Apply to keyboard");
        const std::string target = selectedProfile_ ? "Profile " + std::to_string(*selectedProfile_ + 1)
                                                    : "the keyboard's active profile";
        ImGui::TextWrapped("Overwrite %s on the keyboard with this profile? "
                           "The keyboard's current %s is saved as a backup first, and the result is read back to verify it. "
                           "The keyboard returns to the profile it was on afterwards. "
                           "Do not unplug the keyboard while writing.",
                           target.c_str(), target.c_str());
        if (ImGui::Button("Apply")) { finishDialog(); beginApply(); }
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
        if (ImGui::Button("Restore and apply")) { restore(); finishDialog(); dialog_ = Dialog::Apply; }
        ImGui::EndDisabled();
    }
    if (!dialogError_.empty()) ImGui::TextWrapped("%s", dialogError_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        pending_ = Action::None;
        finishDialog();
    }
    ImGui::EndPopup();
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
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.69f,.80f,.97f,1));
        if (ImGui::Button(layers[i], ImVec2(76,32))) layer_ = i;
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    // Profiles are right-aligned; wrap below the layers when the window is too narrow.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float profilesWidth = ImGui::CalcTextSize("Keyboard profile").x + 4*(96 + spacing);
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
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.69f,.80f,.97f,1));
        const auto label = "Profile " + std::to_string(i+1);
        if (ImGui::Button(label.c_str(), ImVec2(96,32))) selectProfile(i);
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    const std::string caption = summary_ + (unsaved() ? (summary_.empty() ? "Unsaved changes" : "  /  Unsaved changes") : "");
    const float boardHeight = std::max(320.f, ImGui::GetContentRegionAvail().y-115.f);
    ImGui::BeginDisabled(busy);
    if (loaded_) {
        if (const auto slot = drawKeyboard(keymap_, layer_, boardHeight, caption)) {
            slot_ = *slot;
            assignment_.reset(keymap_.scanCode(layer_,slot_));
            dialog_ = Dialog::Assign;
        }
    } else {
        ImGui::BeginChild("No profile", ImVec2(0,boardHeight), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", busy ? "Looking for an HHKB Studio..." : "Connect a keyboard or import a profile to begin.");
        ImGui::EndChild();
    }
    if (ImGui::Button("Read from keyboard")) request(Action::Read);
    ImGui::SetItemTooltip("Read the profile the keyboard is currently using");
    ImGui::SameLine();
    if (ImGui::Button("Import")) request(Action::Import);
    ImGui::SameLine();
    ImGui::BeginDisabled(!loaded_);
    if (ImGui::Button("Export")) openFiles(true);
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults")) dialog_ = Dialog::Defaults;
    ImGui::SameLine();
    ImGui::BeginDisabled(!keymap_.isModified());
    if (ImGui::Button("Discard changes")) keymap_.reset();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!loaded_ || demo_ || busy);
    if (ImGui::Button("Apply to keyboard")) dialog_ = Dialog::Apply;
    ImGui::EndDisabled();
    if (demo_) ImGui::TextDisabled("Demo mode never writes to a keyboard.");
    if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
    drawDialog();
    ImGui::End();
}
