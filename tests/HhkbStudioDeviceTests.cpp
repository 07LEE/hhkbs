#include "device/DeviceDiscovery.h"
#include "device/DeviceError.h"
#include "device/HhkbProtocol.h"
#include "device/HhkbStudioDevice.h"
#include "device/PadMonitor.h"
#include "device/Transport.h"
#include "keymap/Keymap.h"
#include "StorageTransport.h"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
using hhkbs::test::StorageTransport;
using hhkbs::test::patternProfile;
using hhkbs::test::writeText;

void require(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

using hhkbs::device::DeviceError;
using hhkbs::device::DeviceErrorCode;

// For a call that is expected to raise: the check passes when something was raised.
template<typename Raised>
void require(const std::optional<Raised>& error, const std::string& message)
{
    require(error.has_value(), message);
}

// What a call raises, when it raises an exception of type `Exception`; nothing when it raises none of that type.
template<typename Exception, typename Function>
std::optional<Exception> raised(Function&& function)
{
    try {
        function();
    } catch (const Exception& error) {
        return error;
    }
    return std::nullopt;
}

bool mentions(const std::optional<DeviceError>& error, const char* text)
{
    return error && std::string(error->what()).find(text) != std::string::npos;
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

void profileWriteIsVerifiedByReadBack()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();
    const auto profile = patternProfile(3);

    device.requireTarget(2, "serial");
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
    const auto error = raised<DeviceError>([&] { device.writeCurrentProfile(patternProfile(3), backup); });

    require(error.has_value(), "an interrupted write was not reported");
    require(transport.memory == backup, "backup was not restored after a failed write");
}

void mismatchedReadBackRestoresBackup()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();

    // The profile is stored wrongly, but the restore that follows is kept as sent.
    constexpr std::size_t profileWrites = (Keymap::profileByteCount + 25) / 26;
    transport.corruptUntilWrite = profileWrites;
    const auto error = raised<DeviceError>([&] { device.writeCurrentProfile(patternProfile(3), backup); });

    require(error && error->code() == DeviceErrorCode::Protocol, "a read-back mismatch was not reported");
    require(transport.memory == backup, "the previous profile was not put back after a mismatch");
    require(mentions(error, "The previous profile was restored."), "the report does not say the profile was restored");

    // When even the restore is stored wrongly, the report says so.
    StorageTransport stubborn;
    HhkbStudioDevice other(stubborn);
    stubborn.corruptUntilWrite = 2 * profileWrites;
    const auto unrestored = raised<DeviceError>([&] { other.writeCurrentProfile(patternProfile(3), other.readCurrentProfile()); });
    require(mentions(unrestored, "could not be restored"), "a restore that failed was reported as done");
}

void readBackFailureRestoresTheBackup()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);
    const auto backup = device.readCurrentProfile();
    transport.failNextReadAfterWrite = true;

    const auto error = raised<DeviceError>([&] { device.writeCurrentProfile(patternProfile(3), backup); });

    require(error && error->code() == DeviceErrorCode::InputOutput, "a read-back that failed was not reported as a failure of the keyboard");
    require(transport.memory == backup, "the previous profile was not put back when the check could not be done");
    require(mentions(error, "could not be checked") && mentions(error, "was restored"),
            "the report does not say the write could not be checked and was undone");

    // When the check keeps failing, the restore cannot be confirmed either, and the report says so.
    StorageTransport stubborn;
    HhkbStudioDevice other(stubborn);
    stubborn.failReadsAfterWrite = true;
    const auto unconfirmed = raised<DeviceError>([&] { other.writeCurrentProfile(patternProfile(3), other.readCurrentProfile()); });
    require(mentions(unconfirmed, "could not be restored") && mentions(unconfirmed, "saved backup"),
            "an unconfirmed restore was reported as done, or the backup was not mentioned");
}

void anActiveProfileOutOfRangeIsRejected()
{
    StorageTransport transport;
    transport.currentProfile = 7;
    HhkbStudioDevice device(transport);

    const auto error = raised<DeviceError>([&] { static_cast<void>(device.activeProfile()); });
    require(error && error->code() == DeviceErrorCode::Protocol, "a profile number the keyboard does not have was accepted");
    transport.currentProfile = 3;
    require(device.activeProfile() == 3, "the last profile was refused");
}

void writeTargetIsChecked()
{
    StorageTransport wrongProfile;
    HhkbStudioDevice device(wrongProfile);
    require(raised<DeviceError>([&] { device.requireTarget(1, "serial"); }), "a changed active profile was not rejected");

    StorageTransport wrongDevice;
    wrongDevice.productName = "Other";
    HhkbStudioDevice other(wrongDevice);
    require(raised<DeviceError>([&] { other.requireTarget(2, "serial"); }), "a non-HHKB device was not rejected");

    // A second keyboard on the same profile is told apart by its serial number.
    StorageTransport otherKeyboard;
    otherKeyboard.serialNumber = "other-serial";
    HhkbStudioDevice sibling(otherKeyboard);
    require(raised<DeviceError>([&] { sibling.requireTarget(2, "serial"); }), "a keyboard with another serial number was not rejected");

    StorageTransport unknownSerial;
    HhkbStudioDevice unknown(unknownSerial);
    require(raised<DeviceError>([&] { unknown.requireTarget(2, ""); }), "a target without a known serial number was accepted");

    // What is written has to be exactly one profile long, and so does the backup to go back to; nothing is sent otherwise.
    StorageTransport shortProfile;
    HhkbStudioDevice shortDevice(shortProfile);
    require(raised<DeviceError>([&] { shortDevice.writeCurrentProfile({1, 2, 3}, patternProfile(0)); }), "a short profile was accepted");
    require(raised<DeviceError>([&] { shortDevice.writeCurrentProfile(patternProfile(1), {1, 2, 3}); }),
            "a short backup was accepted, and there would be nothing to go back to");
    require(shortProfile.writes == 0, "a rejected profile was still sent");
}

void profileSwitchIsConfirmed()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);

    device.switchProfile(3);

    require(transport.switches == 1, "profile switch was not sent once");
    require(transport.currentProfile == 3, "profile switch did not take effect");
    device.requireTarget(3, "serial");
}

void invalidProfileSwitchIsRejectedBeforeSending()
{
    StorageTransport transport;
    HhkbStudioDevice device(transport);

    require(raised<std::invalid_argument>([&] { device.switchProfile(4); }), "an out-of-range profile was accepted");
    require(transport.switches == 0, "an invalid profile switch was still sent");
}

void unconfirmedProfileSwitchIsReported()
{
    StorageTransport ignored;
    ignored.ignoreSwitch = true;
    HhkbStudioDevice device(ignored);
    const auto error = raised<DeviceError>([&] { device.switchProfile(0); });
    require(error && error->code() == DeviceErrorCode::Protocol, "a switch the keyboard ignored was not reported");

    StorageTransport shortAnswer;
    shortAnswer.switchResponseCount = 1;
    HhkbStudioDevice shortDevice(shortAnswer);
    require(raised<DeviceError>([&] { shortDevice.switchProfile(0); }), "a missing switch response was not reported");

    // Both answers can say the profile was taken while a fresh read of the active profile says otherwise; that one counts.
    StorageTransport doubting;
    HhkbStudioDevice doubtful(doubting);
    doubting.beforeRequest = [](StorageTransport& self, const hhkbs::device::Report& request) {
        const bool profileRead = request[0] == 0x02 && request[1] == 0x11 && request[2] == 0x01;
        if (profileRead && self.switches > 0) self.currentProfile = 2;  // after the switch, it reports where it was
    };
    const auto unconfirmed = raised<DeviceError>([&] { doubtful.switchProfile(0); });
    require(mentions(unconfirmed, "not on the requested profile"), "a switch that the final read does not bear out was accepted");
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
        device.requireTarget(3, "serial");
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

    const auto error = raised<std::runtime_error>([&] { device.runOnProfile(1, [] { throw std::runtime_error("boom"); }); });

    require(error && std::string(error->what()) == "boom", "the action's own error was not propagated");
    require(device.activeProfile() == 2, "the active profile was not restored after an error");
}

void unreturnableProfileIsReported()
{
    StorageTransport transport;
    transport.ignoreSwitchNumber = 2;
    HhkbStudioDevice device(transport);

    const auto error = raised<DeviceError>([&] { device.runOnProfile(1, [] {}); });

    require(mentions(error, "could not be returned to profile 3"), "a keyboard left on another profile was not reported");

    // When the action failed too, the report has both: why it failed, and that the keyboard was left elsewhere.
    StorageTransport both;
    both.ignoreSwitchNumber = 2;
    HhkbStudioDevice twice(both);
    const auto combined = raised<DeviceError>([&] { twice.runOnProfile(1, [] { throw std::runtime_error("the action failed"); }); });
    require(mentions(combined, "the action failed") && mentions(combined, "could not be returned to profile 3"),
            "the reason the action failed was lost when the keyboard could not be returned either");
}

void writeSurvivesFailedReturnToProfile()
{
    StorageTransport transport;
    transport.ignoreSwitchNumber = 2;
    HhkbStudioDevice device(transport);
    const auto profile = patternProfile(9);

    bool written = false;
    const auto error = raised<DeviceError>([&] {
        device.runOnProfile(1, [&] {
            device.writeCurrentProfile(profile, device.readCurrentProfile());
            written = true;
        });
    });

    // The error does not mean the write failed; callers have to track that themselves.
    require(error.has_value(), "a keyboard left on another profile was not reported");
    require(written, "the write did not finish before the return failed");
    require(transport.memory == profile, "the written profile was lost");
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

    // The serial number alone is one request, not the whole set.
    const auto before = transport.requests.size();
    require(device.readSerialNumber() == information.serialNumber, "serial number was not read on its own");
    require(transport.requests.size() == before + 1, "reading the serial number took more than one request");
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

void onlyConfigurationInterfacesAreKept()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("hhkbs-interfaces-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    const auto interface = [&](const std::string& name, const std::vector<std::uint8_t>* descriptor) {
        std::filesystem::create_directories(root / name / "device");
        std::ofstream(root / name / "device" / "uevent") << "HID_ID=0003:000004FE:00000016\nHID_NAME=HHKB-Studio\n";
        if (descriptor) {
            std::ofstream file(root / name / "device" / "report_descriptor", std::ios::binary);
            file.write(reinterpret_cast<const char*>(descriptor->data()), static_cast<std::streamsize>(descriptor->size()));
        }
    };
    const std::vector<std::uint8_t> keys{0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0xC0};
    const std::vector<std::uint8_t> settings{0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01, 0xC0};
    const std::vector<std::uint8_t> otherVendor{0x06, 0x31, 0xFF, 0x09, 0x74, 0xA1, 0x01, 0xC0};
    interface("hidraw-a-keys", &keys);
    interface("hidraw-b-settings", &settings);
    interface("hidraw-c-other", &otherVendor);
    interface("hidraw-d-unreadable", nullptr);

    const auto interfaces = hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces(root);
    std::filesystem::remove_all(root);

    require(interfaces.size() == 2, "the interfaces that cannot take configuration requests were kept");
    require(interfaces[0].path == "/dev/hidraw-b-settings", "the configuration interface was dropped");
    require(interfaces[1].path == "/dev/hidraw-d-unreadable", "an interface whose descriptor cannot be read should be kept");
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

// Answers like the keyboard over Bluetooth: 02 11 01 <slot> <profile>.
class BluetoothTransport final : public Transport {
public:
    [[nodiscard]] bool isBluetooth() const override { return true; }
    [[nodiscard]] Report exchange(const Report& request) override
    {
        if (request[0] == 0x03) sentWrite = true;
        Report response{};
        response[0] = 0x02;
        response[1] = request[1];
        response[2] = request[2];
        response[3] = slot;
        response[4] = profile;
        return response;
    }

    std::uint8_t slot = 1;
    std::uint8_t profile = 0;
    bool sentWrite = false;
};

void bluetoothProfileIsRead()
{
    BluetoothTransport transport;
    HhkbStudioDevice device(transport);
    for (std::uint8_t slot = 1; slot <= 4; ++slot) {
        transport.slot = slot;
        for (std::uint8_t profile = 0; profile < 4; ++profile) {
            transport.profile = profile;
            require(device.activeProfile() == profile, "the Bluetooth profile was read wrongly");
        }
    }

    transport.profile = 4;
    bool rejected = false;
    try {
        static_cast<void>(device.activeProfile());
    } catch (const hhkbs::device::DeviceError&) {
        rejected = true;
    }
    require(rejected, "a profile beyond 4 should be rejected");

    transport.profile = 0;
    bool refused = false;
    try {
        static_cast<void>(device.readProfile(1));
    } catch (const hhkbs::device::DeviceError&) {
        refused = true;
    }
    require(refused, "switching profiles over Bluetooth should be refused");
    require(!transport.sentWrite, "no switch request may be sent over Bluetooth");
}

void bluetoothInterfacesAreTold()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("hhkbs-device-bluetooth-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "hidraw-test-usb" / "device");
    std::filesystem::create_directories(root / "hidraw-test-bluetooth" / "device");

    {
        std::ofstream uevent(root / "hidraw-test-usb" / "device" / "uevent");
        uevent << "HID_ID=0003:000004FE:00000016\n"
               << "HID_NAME=HHKB-Studio\n";
    }
    {
        std::ofstream uevent(root / "hidraw-test-bluetooth" / "device" / "uevent");
        uevent << "HID_ID=0005:000004FE:00000016\n"
               << "HID_NAME=HHKB-Studio\n";
    }

    const auto interfaces =
        hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces(root);
    std::filesystem::remove_all(root);

    require(interfaces.size() == 2, "both connections should be found");
    for (const auto& item : interfaces) {
        require(
            item.bluetooth == (item.path == "/dev/hidraw-test-bluetooth"),
            "only the Bluetooth connection should be marked as Bluetooth");
    }
}

}  // namespace

int main()
{
    try {
        informationCommandsAreDecoded();
        padNotificationsAreDecoded();
        bluetoothInterfacesAreTold();
        bluetoothProfileIsRead();
        gesturePadsAreReadAndSwitched();
        protocolPacketsAreEncodedAndDecoded();
        profileIsReadInBoundedChunks();
        supportedInterfacesAreDiscovered();
        onlyConfigurationInterfacesAreKept();
        writePacketIsEncoded();
        profileWriteIsVerifiedByReadBack();
        failedWriteRestoresBackup();
        mismatchedReadBackRestoresBackup();
        readBackFailureRestoresTheBackup();
        anActiveProfileOutOfRangeIsRejected();
        writeTargetIsChecked();
        writeSurvivesFailedReturnToProfile();
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
