#include "StorageTransport.h"
#include "app/KeyboardTasks.h"
#include "keymap/BackupFiles.h"
#include "keymap/ProfileFiles.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using hhkbs::app::KeyboardAccess;
using hhkbs::device::DeviceInfo;
using hhkbs::device::Transport;
using hhkbs::keymap::Keymap;
using hhkbs::test::StorageTransport;
using hhkbs::test::patternProfile;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// Passes everything on to a keyboard that outlives the connection, so the next connection sees what was written.
class Connection final : public Transport {
public:
    Connection(StorageTransport& keyboard, const bool bluetooth) : keyboard_(keyboard), bluetooth_(bluetooth) {}
    [[nodiscard]] bool isBluetooth() const override { return bluetooth_; }
    [[nodiscard]] hhkbs::device::Report exchange(const hhkbs::device::Report& request) override
    {
        return keyboard_.exchange(request);
    }
    [[nodiscard]] std::vector<hhkbs::device::Report> exchange(const hhkbs::device::Report& request,
                                                              const std::size_t responseCount) override
    {
        return keyboard_.exchange(request, responseCount);
    }

private:
    StorageTransport& keyboard_;
    bool bluetooth_;
};

// A computer with some HHKB interfaces attached; each is a keyboard of its own.
class FakeAccess final : public KeyboardAccess {
public:
    struct Entry {
        DeviceInfo info;
        std::unique_ptr<StorageTransport> keyboard;
    };

    StorageTransport& add(const std::string& serial, const bool bluetooth = false, const bool writable = true)
    {
        Entry entry;
        entry.info.path = "/dev/hidraw" + std::to_string(entries.size());
        entry.info.name = "HHKB";
        entry.info.vendorId = hhkbs::device::hhkbStudioVendorId;
        entry.info.productId = hhkbs::device::hhkbStudioProductId;
        entry.info.canReadWrite = writable;
        entry.info.bluetooth = bluetooth;
        entry.keyboard = std::make_unique<StorageTransport>();
        entry.keyboard->serialNumber = serial;
        entries.push_back(std::move(entry));
        return *entries.back().keyboard;
    }

    std::vector<DeviceInfo> interfaces() override
    {
        std::vector<DeviceInfo> infos;
        for (const auto& entry : entries) infos.push_back(entry.info);
        return infos;
    }

    std::unique_ptr<Transport> open(const DeviceInfo& interface) override
    {
        for (const auto& entry : entries)
            if (entry.info.path == interface.path) return std::make_unique<Connection>(*entry.keyboard, entry.info.bluetooth);
        throw std::runtime_error("no such interface");
    }

    std::vector<Entry> entries;
};

std::filesystem::path backupFolder;

std::size_t backupCount()
{
    return hhkbs::keymap::listBackups(backupFolder).size();
}

void scanReadsTheCurrentProfile()
{
    FakeAccess access;
    auto& keyboard = access.add("A");

    const auto result = hhkbs::app::scanKeyboard(access, std::nullopt, false, "");

    require(result.status == "Connected", "a keyboard that answers was not connected");
    require(result.profile == 2 && result.bytes == keyboard.memory, "the current profile was not read");
    require(result.serial == "A" && result.path == "/dev/hidraw0", "the keyboard was not identified");
    require(contains(result.detail, "Profile 3"), "the profile on screen was not named");
}

void scanReadsAnotherProfileAndLeavesTheKeyboardOnItsOwn()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    const auto wanted = keyboard.stored[0];

    const auto result = hhkbs::app::scanKeyboard(access, 0, false, "A");

    require(result.profile == 0 && result.bytes == wanted, "the chosen profile was not read");
    require(keyboard.currentProfile == 2, "the keyboard was left on another profile");
    require(contains(result.detail, "keyboard is on Profile 3"), "the profile the keyboard is on was not named");
}

void reconnectOnlyFindsTheConnection()
{
    FakeAccess access;
    access.add("A", true);

    const auto result = hhkbs::app::scanKeyboard(access, std::nullopt, true, "");

    require(result.status == "Connected" && result.path == "/dev/hidraw0", "the connection was not found again");
    require(result.bytes.empty() && result.serial.empty(), "a reconnect should not read the profile");
    require(result.bluetooth, "the connection type was not reported");
}

void scanSaysWhyNothingWasFound()
{
    FakeAccess none;
    require(hhkbs::app::scanKeyboard(none, std::nullopt, false, "").status == "No device", "no interface is no device");

    FakeAccess locked;
    locked.add("A", false, false);
    require(hhkbs::app::scanKeyboard(locked, std::nullopt, false, "").status == "Permission required",
            "an interface that cannot be opened was not reported as a permission problem");

    FakeAccess other;
    other.add("A").productName = "Other";
    const auto result = hhkbs::app::scanKeyboard(other, std::nullopt, false, "");
    require(result.status == "Connection failed", "a device that is not an HHKB Studio was accepted");
}

void scanOnlyReadsTheKeyboardThatWasRead()
{
    FakeAccess access;
    access.add("A");
    auto& second = access.add("B");

    const auto wrong = hhkbs::app::scanKeyboard(access, 1, false, "C");
    require(wrong.status == "Connection failed" && wrong.bytes.empty(), "a keyboard with another serial number was read");
    require(contains(wrong.detail, "not connected"), "the reason was not given");

    const auto right = hhkbs::app::scanKeyboard(access, 1, false, "B");
    require(right.serial == "B" && right.path == "/dev/hidraw1" && right.bytes == second.stored[1],
            "the keyboard with the right serial number was not the one read");
}

void applyWritesTheProfileAndKeepsABackup()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    const auto before = keyboard.stored[0];
    const auto work = patternProfile(5);
    const auto backupsBefore = backupCount();

    const auto result = hhkbs::app::applyProfiles(access, {{0, work}}, "A");

    require(result.ok, "applying failed: " + result.message);
    require(result.written == std::vector<std::uint16_t>{0} && result.bytes[0] == work, "the written profile was not reported");
    require(keyboard.stored[0] == work, "the profile was not stored on the keyboard");
    require(keyboard.currentProfile == 2, "the keyboard was not returned to its profile");
    require(backupCount() == backupsBefore + 1, "no backup was saved");
    const auto backup = hhkbs::keymap::listBackups(backupFolder).front();
    require(backup.profile == 0 && hhkbs::keymap::readProfile(backup.path).toBytes() == before,
            "the backup does not hold what the keyboard had");
    require(contains(result.message, "Applied to profile 1") && contains(result.message, backup.path.filename().string()),
            "the message does not name the profile and the backup");
}

void applyWritesSeveralProfilesInOrder()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    const auto first = patternProfile(1);
    const auto second = patternProfile(2);

    const auto result = hhkbs::app::applyProfiles(access, {{1, first}, {3, second}}, "A");

    require(result.ok && result.written == (std::vector<std::uint16_t>{1, 3}), "both profiles should be written in order");
    require(keyboard.stored[1] == first && keyboard.stored[3] == second, "a profile did not reach the keyboard");
    require(contains(result.message, "profiles 2, 4"), "the message does not list both profiles");
}

void applyNeverWritesToAnotherKeyboard()
{
    FakeAccess access;
    auto& first = access.add("A");
    auto& second = access.add("B");
    const auto firstBefore = first.stored[0];
    const auto work = patternProfile(7);

    const auto wrong = hhkbs::app::applyProfiles(access, {{0, work}}, "C");
    require(!wrong.ok && wrong.written.empty(), "applying to a keyboard that is not there succeeded");
    require(first.writes == 0 && second.writes == 0, "something was sent to a keyboard that was not asked for");
    require(contains(wrong.message, "not connected"), "the reason was not given");

    const auto right = hhkbs::app::applyProfiles(access, {{0, work}}, "B");
    require(right.ok, "applying to the second keyboard failed: " + right.message);
    require(second.stored[0] == work, "the second keyboard did not get the profile");
    require(first.stored[0] == firstBefore && first.writes == 0, "the first keyboard was touched");
}

void applyWithoutASerialNumberIsRefused()
{
    FakeAccess access;
    auto& keyboard = access.add("A");

    const auto result = hhkbs::app::applyProfiles(access, {{0, patternProfile(1)}}, "");

    require(!result.ok && keyboard.writes == 0, "applying without knowing which keyboard was read was allowed");
}

void applyNeedsAUsbConnection()
{
    FakeAccess access;
    auto& keyboard = access.add("A", true);

    const auto result = hhkbs::app::applyProfiles(access, {{0, patternProfile(1)}}, "A");

    require(!result.ok && contains(result.message, "USB"), "applying over Bluetooth was not refused with the reason");
    require(keyboard.writes == 0 && keyboard.switches == 0, "something was sent over Bluetooth");
}

void aWriteThatCouldNotBeUndoneIsStillCounted()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    const auto work = patternProfile(9);
    keyboard.ignoreSwitchNumber = 2;  // the switch back to the profile the keyboard was on is ignored

    const auto result = hhkbs::app::applyProfiles(access, {{0, work}}, "A");

    require(!result.ok, "a keyboard left on another profile was reported as fine");
    require(result.written == std::vector<std::uint16_t>{0} && result.bytes[0] == work,
            "a profile that was written was not counted");
    require(contains(result.message, "was written, but") && contains(result.message, "Already written: profile 1"),
            "the message does not say the write happened");
    require(keyboard.memory == work, "the keyboard does not hold the written profile");
}

void aFailedWriteIsReportedAndUndone()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    const auto before = keyboard.stored[0];
    keyboard.failWriteNumber = 5;

    const auto result = hhkbs::app::applyProfiles(access, {{0, patternProfile(4)}}, "A");

    require(!result.ok && result.written.empty(), "a failed write was counted");
    require(contains(result.message, "Profile 1 was not written"), "the message does not say the profile was not written");
    require(keyboard.stored[0] == before && keyboard.currentProfile == 2, "the keyboard was not put back as it was");
}

void aFailureStopsTheProfilesThatFollow()
{
    FakeAccess access;
    auto& keyboard = access.add("A");
    keyboard.failWriteNumber = 5;
    keyboard.stored[2] = patternProfile(8);
    keyboard.memory = keyboard.stored[2];
    const auto untouched = keyboard.stored[3];

    const auto result = hhkbs::app::applyProfiles(access, {{0, patternProfile(1)}, {3, patternProfile(2)}}, "A");

    require(!result.ok && result.written.empty(), "the first failure should be reported");
    require(keyboard.stored[3] == untouched, "a profile after the failure was still written");
}

void profilesAreReadFromTheRightKeyboard()
{
    FakeAccess access;
    access.add("A");
    auto& second = access.add("B");

    const auto result = hhkbs::app::readProfiles(access, {0, 1, 3}, "B");

    require(result.profiles.size() == 3, "a profile was left out");
    for (const auto& read : result.profiles)
        require(read.error.empty() && read.bytes == second.stored[read.profile], "a profile was not read from the keyboard");
    require(second.currentProfile == 2, "reading left the keyboard on another profile");
}

void profilesAreNotReadFromAnotherKeyboardOrOverBluetooth()
{
    FakeAccess access;
    access.add("A");
    const auto other = hhkbs::app::readProfiles(access, {0, 1}, "C");
    require(other.profiles.size() == 2, "every profile asked for should be answered");
    for (const auto& read : other.profiles)
        require(!read.error.empty() && read.bytes.empty(), "a profile was read from a keyboard that was not asked for");

    FakeAccess wireless;
    wireless.add("A", true);
    const auto blue = hhkbs::app::readProfiles(wireless, {0}, "A");
    require(contains(blue.profiles[0].error, "USB"), "reading over Bluetooth was not refused with the reason");
}

void aPadChangeNeedsTheKeyboardThatWasRead()
{
    FakeAccess access;
    auto& keyboard = access.add("A");

    const auto result = hhkbs::app::changePad(access, 0, false, "C");

    require(!result.ok && contains(result.message, "not connected"), "a pad of another keyboard was changed");
    require(keyboard.writes == 0, "something was sent to the wrong keyboard");
}

}  // namespace

int main()
{
    char name[] = "/tmp/hhkbs-tasks-XXXXXX";
    const char* created = ::mkdtemp(name);
    if (!created) return 1;
    const std::filesystem::path directory(created);
    ::setenv("XDG_STATE_HOME", directory.c_str(), 1);
    backupFolder = hhkbs::keymap::backupDirectory();
    int result = 0;
    try {
        scanReadsTheCurrentProfile();
        scanReadsAnotherProfileAndLeavesTheKeyboardOnItsOwn();
        reconnectOnlyFindsTheConnection();
        scanSaysWhyNothingWasFound();
        scanOnlyReadsTheKeyboardThatWasRead();
        applyWritesTheProfileAndKeepsABackup();
        applyWritesSeveralProfilesInOrder();
        applyNeverWritesToAnotherKeyboard();
        applyWithoutASerialNumberIsRefused();
        applyNeedsAUsbConnection();
        aWriteThatCouldNotBeUndoneIsStillCounted();
        aFailedWriteIsReportedAndUndone();
        aFailureStopsTheProfilesThatFollow();
        profilesAreReadFromTheRightKeyboard();
        profilesAreNotReadFromAnotherKeyboardOrOverBluetooth();
        aPadChangeNeedsTheKeyboardThatWasRead();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return result;
}
