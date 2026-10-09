#include "app/KeyboardTasks.h"
#include "device/DeviceDiscovery.h"
#include "device/HhkbStudioDevice.h"
#include "device/HidrawTransport.h"
#include "keymap/BackupFiles.h"
#include "keymap/Keymap.h"
#include "keymap/ProfileFiles.h"
#include <ctime>
#include <exception>
#include <memory>
#include <stdexcept>

namespace hhkbs::app {
namespace {

// With usbOnly, a Bluetooth connection is passed over: writing a profile is only done over the cable.
// With a serial number, only the keyboard that carries it is opened, so a second HHKB Studio is never mistaken for it.
std::unique_ptr<hhkbs::device::Transport> openStudio(KeyboardAccess& access, const bool usbOnly, const std::string& serial)
{
    bool skippedBluetooth = false;
    bool otherKeyboard = false;
    for (const auto& item : access.interfaces()) {
        if (!item.canReadWrite) continue;
        if (usbOnly && item.bluetooth) { skippedBluetooth = true; continue; }
        try {
            auto transport = access.open(item);
            hhkbs::device::HhkbStudioDevice device(*transport);
            if (device.readProductName() != "HHKB-Studio") continue;
            if (serial.empty() || device.readSerialNumber() == serial) return transport;
            otherKeyboard = true;
        } catch (const std::exception&) {}
    }
    if (otherKeyboard) throw std::runtime_error("The keyboard that was read is not connected. Read from the keyboard again.");
    if (skippedBluetooth) throw std::runtime_error("Applying needs a USB connection. Connect the keyboard with a cable.");
    throw std::runtime_error("No writable HHKB Studio was found. Check the connection and udev rules.");
}

class HidrawAccess final : public KeyboardAccess {
public:
    std::vector<hhkbs::device::DeviceInfo> interfaces() override
    {
        return hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces();
    }
    std::unique_ptr<hhkbs::device::Transport> open(const hhkbs::device::DeviceInfo& interface) override
    {
        return std::make_unique<hhkbs::device::HidrawTransport>(interface.path);
    }
};

// `text` as a sentence, so that another can follow it.
std::string sentence(std::string text)
{
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') text += '.';
    return text;
}

}  // namespace

KeyboardAccess& hidrawAccess()
{
    static HidrawAccess access;
    return access;
}

ScanResult scanKeyboard(KeyboardAccess& access, const std::optional<std::uint16_t> target, const bool reconnect,
                        const std::string& serial)
{
    ScanResult result{"No device", "Connect an HHKB Studio, import a TOML profile, or start with --demo.", {}, std::nullopt, {}, false, {}, {}};
    try {
        const auto devices = access.interfaces();
        bool permission = false;
        bool otherKeyboard = false;  // a keyboard answered, but not the one that was read
        std::string firstError;  // the first interface that failed says why; the ones after it often just time out
        for (const auto& item : devices) {
            if (!item.canReadWrite) { permission = true; continue; }
            try {
                const auto transport = access.open(item);
                hhkbs::device::HhkbStudioDevice device(*transport);
                if (device.readProductName() != "HHKB-Studio") continue;
                if (reconnect) {
                    // Only bring the connection back; the profile is read when the user asks for it. It has to be the
                    // keyboard that was read, not another one that happens to be plugged in.
                    if (!serial.empty() && device.readSerialNumber() != serial) { otherKeyboard = true; continue; }
                    result.path = item.path;
                    result.bluetooth = item.bluetooth;
                    for (std::size_t pad = 0; pad < result.pads.size(); ++pad) {
                        try { result.pads[pad] = device.padState(pad); } catch (const std::exception&) {}
                    }
                    result.status = "Connected";
                    return result;
                }
                const auto info = device.readInformation();
                if (!serial.empty() && info.serialNumber != serial) { otherKeyboard = true; continue; }
                const auto profile = target.value_or(info.currentProfile);
                result.bytes = device.readProfile(profile);
                result.profile = profile;
                result.serial = info.serialNumber;
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
            } catch (const std::exception& error) { if (firstError.empty()) firstError = error.what(); }
        }
        if (permission) {
            result.status = "Permission required";
            result.detail = "Install packaging/60-hhkbs.rules as described in the README, then reconnect the keyboard.";
        } else if (!devices.empty()) {
            result.status = "Connection failed";
            // Other interfaces of the same keyboard time out after this one answered; that must not hide the cause.
            if (otherKeyboard) result.detail = "The keyboard that was read is not connected. Read from the keyboard again.";
            else result.detail = firstError.empty() ? "No configuration interface responded." : firstError;
        }
    } catch (const std::exception& error) {
        result.status = "Connection failed";
        result.detail = error.what();
    }
    return result;
}

PadResult changePad(KeyboardAccess& access, const std::size_t pad, const bool on, const std::string& serial)
{
    PadResult result{false, pad, on, {}};
    try {
        auto transport = openStudio(access, false, serial);
        hhkbs::device::HhkbStudioDevice(*transport).setPadState(pad, on);
        result.ok = true;
    } catch (const std::exception& error) { result.message = error.what(); }
    return result;
}

ApplyResult applyProfiles(KeyboardAccess& access,
                          const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>>& jobs,
                          const std::string& serial)
{
    ApplyResult result;
    const auto listed = [](const std::vector<std::uint16_t>& profiles) {
        std::string text = profiles.size() == 1 ? "profile " : "profiles ";
        for (std::size_t i = 0; i < profiles.size(); ++i) text += (i ? ", " : "") + std::to_string(profiles[i] + 1);
        return text;
    };
    std::string backups;
    try {
        auto transport = openStudio(access, true, serial);
        hhkbs::device::HhkbStudioDevice device(*transport);
        for (const auto& [profile, bytes] : jobs) {
            std::filesystem::path path;
            bool written = false;
            const auto record = [&] {
                result.written.push_back(profile);
                result.bytes[profile] = bytes;
                backups += (backups.empty() ? "" : ", ") + path.filename().string();
            };
            try {
                device.runOnProfile(profile, [&] {
                    device.requireTarget(profile, serial);
                    const auto backup = device.readCurrentProfile();

                    // Keep a copy of what the keyboard held; without it a failed write cannot be undone by hand.
                    const auto directory = hhkbs::keymap::backupDirectory();
                    std::filesystem::create_directories(directory);
                    path = hhkbs::keymap::newBackupPath(directory, std::time(nullptr), profile);
                    hhkbs::keymap::writeProfile(path, hhkbs::keymap::Keymap(backup), false);

                    // The backup took a moment, and the keyboard can be switched with its own keys meanwhile.
                    device.requireTarget(profile, serial);
                    device.writeCurrentProfile(bytes, backup);
                    written = true;
                });
            } catch (const std::exception& error) {
                // The write can succeed and the keyboard still fail to return to its profile afterwards.
                if (!written) {
                    // Without the name of the backup there would be nothing to recover from by hand.
                    const std::string saved = path.empty() ? "" : " The previous content is saved as " + path.filename().string() + ".";
                    throw std::runtime_error("Profile " + std::to_string(profile + 1) + " was not written: " + sentence(error.what()) + saved);
                }
                record();
                throw std::runtime_error("Profile " + std::to_string(profile + 1) + " was written, but " + sentence(error.what()));
            }
            record();
        }
        result.ok = true;
        result.message = "Applied to " + listed(result.written) + ". The previous content was saved as " + backups + ".";
    } catch (const std::exception& error) {
        result.message = error.what();
        if (!result.written.empty()) {
            result.message += " Already written: " + listed(result.written) + ". Their previous content was saved as " + backups + ".";
        }
    }
    return result;
}

PreviewResult readProfiles(KeyboardAccess& access, const std::vector<std::uint16_t>& profiles,
                           const std::string& serial)
{
    PreviewResult result;
    std::unique_ptr<hhkbs::device::Transport> transport;
    std::string failure;
    try { transport = openStudio(access, true, serial); } catch (const std::exception& error) { failure = error.what(); }
    for (const auto profile : profiles) {
        ProfileRead read{profile, {}, failure};
        if (failure.empty()) {
            try { read.bytes = hhkbs::device::HhkbStudioDevice(*transport).readProfile(profile); }
            catch (const std::exception& error) { read.error = error.what(); }
        }
        result.profiles.push_back(std::move(read));
    }
    return result;
}

}  // namespace hhkbs::app
