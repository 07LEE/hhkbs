#include "device/DeviceDiscovery.h"
#include "device/DeviceError.h"
#include "device/HhkbProtocol.h"
#include "device/HhkbStudioDevice.h"
#include "device/PadMonitor.h"
#include "device/Transport.h"
#include "keymap/Keymap.h"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

using hhkbs::device::HhkbStudioDevice;
using hhkbs::device::Report;
using hhkbs::device::Transport;
using hhkbs::keymap::Keymap;

void require(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeText(Report& report, const std::string& text)
{
    for (std::size_t index = 0; index < text.size() && index + 3 < report.size(); ++index) {
        report[index + 3] = static_cast<std::uint8_t>(text[index]);
    }
}

class FakeTransport final : public Transport {
public:
    [[nodiscard]] Report exchange(const Report& request) override
    {
        requests.push_back(request);
        Report response{};

        if (request[0] == 0x12) {
            const auto offset = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(request[1]) << 8U)
                | static_cast<std::uint16_t>(request[2]);
            for (std::uint8_t index = 0; index < request[3]; ++index) {
                response[4 + index] =
                    static_cast<std::uint8_t>((offset + index) & 0xFFU);
            }
            return response;
        }

        const auto command = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(request[1]) << 8U)
            | static_cast<std::uint16_t>(request[2]);
        switch (command) {
        case 0x1001:
            writeText(response, "HHKB-Studio");
            break;
        case 0x1002:
            writeText(response, "US");
            break;
        case 0x1003:
            writeText(response, "1.0");
            break;
        case 0x1005:
            writeText(response, "PD-ID100");
            break;
        case 0x1007:
            writeText(response, "serial");
            break;
        case 0x100B:
            writeText(response, "A2.00");
            break;
        case 0x1101:
            response[3] = 0;
            response[4] = 2;
            break;
        case 0x1103:
            response[3] = 1;
            response[4] = 0;
            response[5] = 1;
            response[6] = 0;
            response[7] = 1;
            response[8] = 0;
            break;
        default:
            break;
        }
        return response;
    }

    std::vector<Report> requests;
};

// Behaves like a keyboard that stores its profile, so writes can be read back.
class StorageTransport final : public Transport {
public:
    // Each profile holds its own data; `memory` is the active one.
    StorageTransport()
        : memory(Keymap::profileByteCount)
    {
        for (std::size_t profile = 0; profile < stored.size(); ++profile) {
            stored[profile].assign(Keymap::profileByteCount, 0);
            for (std::size_t index = 0; index < Keymap::profileByteCount; ++index) {
                stored[profile][index] =
                    static_cast<std::uint8_t>(index * 7U + profile * 31U);
            }
        }
        memory = stored.at(currentProfile);
    }

    using Transport::exchange;

    // Mirrors the keyboard: a profile switch is answered by two reports.
    [[nodiscard]] std::vector<Report> exchange(
        const Report& request,
        const std::size_t responseCount) override
    {
        if (request[0] != 0x03) {
            return Transport::exchange(request, responseCount);
        }
        ++switches;
        const std::uint8_t requested = request[4];
        const bool ignored = ignoreSwitch
            || (ignoreSwitchNumber != 0 && switches == ignoreSwitchNumber);
        if (!ignored) {
            stored.at(currentProfile) = memory;
            currentProfile = requested;
            memory = stored.at(currentProfile);
        }
        Report notification{};
        notification[0] = 0x02;
        notification[1] = 0x11;
        notification[2] = 0x01;
        notification[4] = ignored ? currentProfile : requested;
        notification[7] = 0x20;
        Report acknowledgement = notification;
        acknowledgement[0] = 0x03;
        acknowledgement[7] = 0;
        std::vector<Report> responses{notification, acknowledgement};
        responses.resize(switchResponseCount, acknowledgement);
        return responses;
    }

    [[nodiscard]] Report exchange(const Report& request) override
    {
        Report response{};
        const auto address = static_cast<std::size_t>(
            (static_cast<std::uint16_t>(request[1]) << 8U) | request[2]);

        if (request[0] == 0x12) {
            for (std::size_t index = 0; index < request[3]; ++index) {
                response[4 + index] = memory.at(address + index);
            }
        } else if (request[0] == 0x13) {
            ++writes;
            if (failWriteNumber != 0 && writes == failWriteNumber) {
                throw hhkbs::device::DeviceError(
                    hhkbs::device::DeviceErrorCode::Disconnected,
                    "unplugged");
            }
            for (std::size_t index = 0; index < request[3]; ++index) {
                const auto value = request[4 + index];
                memory.at(address + index) =
                    corruptWrites ? static_cast<std::uint8_t>(~value) : value;
            }
        } else if (address == 0x1001) {
            writeText(response, productName);
        } else if (address == 0x1101) {
            response[4] = currentProfile;
        }
        return response;
    }

    std::vector<std::uint8_t> memory;
    std::array<std::vector<std::uint8_t>, 4> stored;
    std::string productName = "HHKB-Studio";
    std::uint8_t currentProfile = 2;
    std::size_t writes = 0;
    std::size_t switches = 0;
    std::size_t switchResponseCount = 2;
    bool ignoreSwitch = false;
    std::size_t ignoreSwitchNumber = 0;
    std::size_t failWriteNumber = 0;
    bool corruptWrites = false;
};

std::vector<std::uint8_t> patternProfile(const std::uint8_t seed)
{
    std::vector<std::uint8_t> profile(Keymap::profileByteCount);
    for (std::size_t index = 0; index < profile.size(); ++index) {
        profile[index] = static_cast<std::uint8_t>(index + seed);
    }
    return profile;
}

void profileWriteIsVerifiedByReadBack()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();
    const auto profile = patternProfile(3);

    device.requireTarget(2);
    device.writeCurrentProfile(profile, backup);

    require(transport.memory == profile, "profile was not stored on the device");
    constexpr std::size_t expectedWrites = (Keymap::profileByteCount + 25) / 26;
    require(transport.writes == expectedWrites, "profile used unexpected write chunks");
}

void failedWriteRestoresBackup()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();

    transport.failWriteNumber = 5;
    bool threw = false;
    try {
        device.writeCurrentProfile(patternProfile(3), backup);
    } catch (const hhkbs::device::DeviceError&) {
        threw = true;
    }

    require(threw, "an interrupted write was not reported");
    require(transport.memory == backup, "backup was not restored after a failed write");
}

void mismatchedReadBackRestoresBackup()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();

    transport.corruptWrites = true;
    bool threw = false;
    try {
        device.writeCurrentProfile(patternProfile(3), backup);
    } catch (const hhkbs::device::DeviceError& error) {
        threw = error.code() == hhkbs::device::DeviceErrorCode::Protocol;
    }

    require(threw, "a read-back mismatch was not reported");
}

void writeTargetIsChecked()
{
    StorageTransport wrongProfile;
    HhkbStudioDevice device(wrongProfile);
    bool threw = false;
    try {
        device.requireTarget(1);
    } catch (const hhkbs::device::DeviceError&) {
        threw = true;
    }
    require(threw, "a changed active profile was not rejected");

    StorageTransport wrongDevice;
    wrongDevice.productName = "Other";
    HhkbStudioDevice other(wrongDevice);
    threw = false;
    try {
        other.requireTarget(2);
    } catch (const hhkbs::device::DeviceError&) {
        threw = true;
    }
    require(threw, "a non-HHKB device was not rejected");

    StorageTransport shortProfile;
    HhkbStudioDevice shortDevice(shortProfile);
    threw = false;
    try {
        shortDevice.writeCurrentProfile({1, 2, 3}, patternProfile(0));
    } catch (const hhkbs::device::DeviceError&) {
        threw = true;
    }
    require(threw, "a short profile was accepted");
    require(shortProfile.writes == 0, "a rejected profile was still sent");
}

void profileSwitchIsConfirmed()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);

    device.switchProfile(3);

    require(transport.switches == 1, "profile switch was not sent once");
    require(transport.currentProfile == 3, "profile switch did not take effect");
    device.requireTarget(3);
}

void invalidProfileSwitchIsRejectedBeforeSending()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);

    bool threw = false;
    try {
        device.switchProfile(4);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    require(threw, "an out-of-range profile was accepted");
    require(transport.switches == 0, "an invalid profile switch was still sent");
}

void unconfirmedProfileSwitchIsReported()
{
    StorageTransport ignored;
    ignored.ignoreSwitch = true;
    HhkbStudioDevice device(ignored);
    bool threw = false;
    try {
        device.switchProfile(0);
    } catch (const hhkbs::device::DeviceError& error) {
        threw = error.code() == hhkbs::device::DeviceErrorCode::Protocol;
    }
    require(threw, "a switch the keyboard ignored was not reported");

    StorageTransport shortAnswer;
    shortAnswer.switchResponseCount = 1;
    HhkbStudioDevice shortDevice(shortAnswer);
    threw = false;
    try {
        shortDevice.switchProfile(0);
    } catch (const hhkbs::device::DeviceError&) {
        threw = true;
    }
    require(threw, "a missing switch response was not reported");
}

void otherProfileIsReadAndActiveProfileIsKept()
{
    StorageTransport transport;
    const auto activeData = transport.memory;
    const auto otherData = transport.stored.at(0);
    HhkbStudioDevice device(transport);

    const auto profile = device.readProfile(0);

    require(profile == otherData, "the requested profile was not read");
    require(device.activeProfile() == 2, "the active profile was not restored");
    require(transport.memory == activeData, "the active profile data changed");
    require(transport.switches == 2, "reading another profile should switch out and back");

    const auto before = transport.switches;
    require(device.readProfile(2) == activeData, "the active profile was not read directly");
    require(transport.switches == before, "reading the active profile should not switch");
}

void writeToOtherProfileKeepsActiveProfile()
{
    StorageTransport transport;
    const auto activeData = transport.memory;
    HhkbStudioDevice device(transport);
    const auto profile = patternProfile(9);

    device.runOnProfile(3, [&] {
        device.requireTarget(3);
        device.writeCurrentProfile(profile, device.readCurrentProfile());
    });

    require(device.activeProfile() == 2, "the active profile was not restored after writing");
    require(transport.memory == activeData, "the active profile was modified");
    require(transport.stored.at(3) == profile, "the target profile did not receive the data");
}

void failedActionStillRestoresActiveProfile()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);

    bool threw = false;
    try {
        device.runOnProfile(1, [] { throw std::runtime_error("boom"); });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "boom";
    }

    require(threw, "the action's own error was not propagated");
    require(device.activeProfile() == 2, "the active profile was not restored after an error");
}

void unreturnableProfileIsReported()
{
    StorageTransport transport;
    transport.ignoreSwitchNumber = 2;
    HhkbStudioDevice device(transport);

    bool threw = false;
    try {
        device.runOnProfile(1, [] {});
    } catch (const hhkbs::device::DeviceError& error) {
        threw = std::string(error.what()).find("could not be returned to profile 3")
            != std::string::npos;
    }

    require(threw, "a keyboard left on another profile was not reported");
}

void profileSwitchPacketIsEncoded()
{
    const auto request = hhkbs::device::protocol::encodeProfileSwitchRequest(2);
    require(
        request[0] == 0x03 && request[1] == 0x11 && request[2] == 0x01
            && request[3] == 0x00 && request[4] == 0x02 && request[5] == 0,
        "profile switch request was encoded incorrectly");
}

void writePacketIsEncoded()
{
    const auto request = hhkbs::device::protocol::encodeDataWriteRequest(
        0x0102,
        {0xAA, 0xBB, 0xCC});
    require(
        request[0] == 0x13 && request[1] == 0x01 && request[2] == 0x02
            && request[3] == 3 && request[4] == 0xAA && request[6] == 0xCC
            && request[7] == 0,
        "write request was encoded incorrectly");
}

void informationCommandsAreDecoded()
{
    FakeTransport transport;
    HhkbStudioDevice device(transport);

    const auto information = device.readInformation();

    require(information.productName == "HHKB-Studio", "product name was not decoded");
    require(information.modelName == "PD-ID100", "model name was not decoded");
    require(information.keyboardLayout == "US", "layout was not decoded");
    require(information.firmwareVersion == "A2.00", "firmware was not decoded");
    require(information.currentProfile == 2, "current profile was not decoded");
    require(information.dipSwitches == 0b101010, "DIP switches were not packed");
    require(transport.requests.size() == 8, "unexpected information request count");
    require(
        transport.requests.front()[0] == 0x02
            && transport.requests.front()[1] == 0x10
            && transport.requests.front()[2] == 0x01,
        "product request was encoded incorrectly");
}

void protocolPacketsAreEncodedAndDecoded()
{
    const auto propertyRequest =
        hhkbs::device::protocol::encodePropertyRequest(
            hhkbs::device::protocol::Property::FirmwareVersion);
    require(
        propertyRequest[0] == 0x02
            && propertyRequest[1] == 0x10
            && propertyRequest[2] == 0x0B,
        "property request was encoded incorrectly");

    const auto dataRequest =
        hhkbs::device::protocol::encodeDataReadRequest(0x1234, 26);
    require(
        dataRequest[0] == 0x12
            && dataRequest[1] == 0x12
            && dataRequest[2] == 0x34
            && dataRequest[3] == 26,
        "data request was encoded incorrectly");

    Report response{};
    response[3] = 0xAB;
    response[4] = 0xCD;
    require(
        hhkbs::device::protocol::decodeBigEndian16(response, 3) == 0xABCD,
        "big-endian value was decoded incorrectly");
}

void padNotificationsAreDecoded()
{
    namespace protocol = hhkbs::device::protocol;
    // Reports captured from a keyboard: 02 11 05 01 <pad> <state>.
    Report frontRightOn{0x02, 0x11, 0x05, 0x01, 0x02, 0x01};
    require(protocol::isNotification(frontRightOn), "a pad report must be recognised as a notification");
    const auto change = protocol::decodePadNotification(frontRightOn);
    require(change && change->pad == 2 && change->on, "front right on was decoded incorrectly");
    const auto leftOff = protocol::decodePadNotification(Report{0x02, 0x11, 0x05, 0x01, 0x00, 0x00});
    require(leftOff && leftOff->pad == 0 && !leftOff->on, "left side off was decoded incorrectly");

    require(!protocol::decodePadNotification(Report{0x02, 0x11, 0x05, 0x01, 0x04, 0x01}),
            "a pad outside the four must be ignored");
    require(!protocol::decodePadNotification(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x02}),
            "an unknown pad state must be ignored");
    require(!protocol::decodePadNotification(Report{0x02, 0x11, 0x05, 0x02, 0x01, 0x01}),
            "a notification of another kind must be ignored");
    require(!protocol::isNotification(Report{0x02, 0x11, 0x01, 0x00, 0x00}),
            "the answer to a property request is not a notification");
}

// A keyboard whose four gesture pads can be read and switched. `dropChanges` makes it acknowledge without acting.
class PadTransport final : public Transport {
public:
    [[nodiscard]] Report exchange(const Report& request) override
    {
        Report response = request;
        const auto pad = request[4];
        if (request[0] == 0x03) {
            if (!dropChanges) on.at(pad) = request[5] != 0;
            return response;
        }
        response[5] = on.at(pad) ? 1 : 0;
        return response;
    }

    std::array<bool, 4> on{true, true, true, true};
    bool dropChanges = false;
};

void gesturePadsAreReadAndSwitched()
{
    namespace protocol = hhkbs::device::protocol;
    require(protocol::encodePadStateRequest(2) == (Report{0x02, 0x11, 0x05, 0x01, 0x02}), "pad request was encoded incorrectly");
    require(protocol::encodePadStateWrite(3, false) == (Report{0x03, 0x11, 0x05, 0x01, 0x03, 0x00}), "pad off was encoded incorrectly");
    require(protocol::encodePadStateWrite(0, true) == (Report{0x03, 0x11, 0x05, 0x01, 0x00, 0x01}), "pad on was encoded incorrectly");
    bool rejected = false;
    try { static_cast<void>(protocol::encodePadStateRequest(4)); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "a fifth gesture pad must be rejected before sending");

    PadTransport transport;
    HhkbStudioDevice device(transport);
    require(device.padState(1), "front left should read as on");
    device.setPadState(1, false);
    require(!device.padState(1) && transport.on[0] && transport.on[2] && transport.on[3],
            "switching one pad must change only that pad");
    device.setPadState(1, true);
    require(device.padState(1), "front left should be on again");

    transport.dropChanges = true;
    bool caught = false;
    try { device.setPadState(2, false); } catch (const hhkbs::device::DeviceError&) { caught = true; }
    require(caught, "a change the keyboard did not keep must be reported");
}

void padMonitorFollowsNotifications()
{
    using hhkbs::device::PadMonitor;
    const auto fifo = std::filesystem::temp_directory_path() / ("hhkbs-pad-" + std::to_string(::getpid()));
    require(::mkfifo(fifo.c_str(), 0600) == 0, "could not create the fake device");

    PadMonitor monitor;
    require(monitor.state(1) == PadMonitor::State::Unknown, "a pad must be unknown before any report");
    monitor.start(fifo);
    const int writer = ::open(fifo.c_str(), O_WRONLY);
    const auto send = [&](const Report& report) { require(::write(writer, report.data(), report.size()) == 32, "write failed"); };
    const auto waitFor = [&](const std::size_t pad, const PadMonitor::State expected) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (monitor.state(pad) == expected) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    };

    send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x01});
    require(waitFor(1, PadMonitor::State::On), "front left should be on after its report");
    send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x00});
    require(waitFor(1, PadMonitor::State::Off), "front left should be off after its report");
    require(monitor.state(0) == PadMonitor::State::Unknown, "a pad that never reported stays unknown");

    monitor.stop();
    ::close(writer);
    ::unlink(fifo.c_str());
    require(monitor.state(1) == PadMonitor::State::Unknown, "pads are unknown once listening stops");
}

void profileIsReadInBoundedChunks()
{
    FakeTransport transport;
    HhkbStudioDevice device(transport);

    const auto profile = device.readCurrentProfile();

    require(profile.size() == Keymap::profileByteCount, "profile length is incorrect");
    for (std::size_t index = 0; index < profile.size(); ++index) {
        require(
            profile[index] == static_cast<std::uint8_t>(index & 0xFFU),
            "profile chunk was assembled at the wrong offset");
    }

    constexpr std::size_t expectedRequestCount =
        (Keymap::profileByteCount + 25) / 26;
    require(
        transport.requests.size() == expectedRequestCount,
        "profile used an unexpected number of chunk requests");
    require(transport.requests.front()[3] == 26, "full chunk length is incorrect");
    require(transport.requests.back()[3] == 24, "final chunk length is incorrect");
}

void supportedInterfacesAreDiscovered()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("hhkbs-device-discovery-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "hidraw-test-hhkb" / "device");
    std::filesystem::create_directories(root / "hidraw-test-other" / "device");

    {
        std::ofstream uevent(root / "hidraw-test-hhkb" / "device" / "uevent");
        uevent << "HID_ID=0003:000004FE:00000016\n"
               << "HID_NAME=HHKB-Studio\n";
    }
    {
        std::ofstream uevent(root / "hidraw-test-other" / "device" / "uevent");
        uevent << "HID_ID=0003:00001234:00005678\n"
               << "HID_NAME=Other keyboard\n";
    }

    const auto interfaces =
        hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces(root);
    std::filesystem::remove_all(root);

    require(interfaces.size() == 1, "device discovery did not filter by USB ID");
    require(interfaces.front().name == "HHKB-Studio", "device name was not parsed");
    require(
        interfaces.front().vendorId == hhkbs::device::hhkbStudioVendorId,
        "vendor ID was not parsed");
    require(
        interfaces.front().productId == hhkbs::device::hhkbStudioProductId,
        "product ID was not parsed");
    require(
        interfaces.front().path == "/dev/hidraw-test-hhkb",
        "hidraw device path was assembled incorrectly");
}

}  // namespace

int main()
{
    try {
        informationCommandsAreDecoded();
        padNotificationsAreDecoded();
        gesturePadsAreReadAndSwitched();
        padMonitorFollowsNotifications();
        protocolPacketsAreEncodedAndDecoded();
        profileIsReadInBoundedChunks();
        supportedInterfacesAreDiscovered();
        writePacketIsEncoded();
        profileWriteIsVerifiedByReadBack();
        failedWriteRestoresBackup();
        mismatchedReadBackRestoresBackup();
        writeTargetIsChecked();
        profileSwitchPacketIsEncoded();
        profileSwitchIsConfirmed();
        invalidProfileSwitchIsRejectedBeforeSending();
        unconfirmedProfileSwitchIsReported();
        otherProfileIsReadAndActiveProfileIsKept();
        writeToOtherProfileKeepsActiveProfile();
        failedActionStillRestoresActiveProfile();
        unreturnableProfileIsReported();
    } catch (const std::exception& error) {
        std::cerr << "HHKB device test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All HHKB device tests passed\n";
    return 0;
}
