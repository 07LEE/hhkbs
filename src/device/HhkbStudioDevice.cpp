#include "device/HhkbStudioDevice.h"

#include "device/DeviceError.h"
#include "keymap/Keymap.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>

namespace hhkbs::device {

HhkbStudioDevice::HhkbStudioDevice(Transport& transport)
    : transport_(transport)
{
}

std::string HhkbStudioDevice::readProductName()
{
    return protocol::decodeText(readProperty(protocol::Property::ProductName));
}

KeyboardInformation HhkbStudioDevice::readInformation()
{
    const auto textProperty = [this](const protocol::Property property) {
        return protocol::decodeText(readProperty(property));
    };

    KeyboardInformation information{
        .productName = textProperty(protocol::Property::ProductName),
        .modelName = textProperty(protocol::Property::ModelName),
        .serialNumber = textProperty(protocol::Property::SerialNumber),
        .keyboardLayout = textProperty(protocol::Property::KeyboardLayout),
        .bootLoaderVersion = textProperty(protocol::Property::BootLoaderVersion),
        .firmwareVersion = textProperty(protocol::Property::FirmwareVersion),
    };

    information.dipSwitches = protocol::decodeBitBytes(
        readProperty(protocol::Property::DipSwitches),
        protocol::textPayloadOffset,
        6);
    information.currentProfile = activeProfile();
    return information;
}

std::vector<std::uint8_t> HhkbStudioDevice::readCurrentProfile()
{
    static_assert(
        keymap::Keymap::profileByteCount <= std::numeric_limits<std::uint16_t>::max());
    return readData(
        0,
        static_cast<std::uint16_t>(keymap::Keymap::profileByteCount));
}

std::uint16_t HhkbStudioDevice::activeProfile()
{
    const auto response = readProperty(protocol::Property::CurrentProfile);
    if (!transport_.isBluetooth()) {
        return protocol::decodeBigEndian16(response, protocol::textPayloadOffset);
    }
    // Over Bluetooth: 02 11 01 01 <profile>, the profile counted from 0 as over USB.
    const auto profile = response[protocol::textPayloadOffset + 1];
    if (response[protocol::textPayloadOffset] != 0x01 || profile >= protocol::profileCount) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio reported an unexpected profile over Bluetooth");
    }
    return profile;
}

std::vector<std::uint8_t> HhkbStudioDevice::readProfile(const std::uint16_t profile)
{
    std::vector<std::uint8_t> data;
    runOnProfile(profile, [&] { data = readCurrentProfile(); });
    return data;
}

void HhkbStudioDevice::runOnProfile(
    const std::uint16_t profile,
    const std::function<void()>& action)
{
    if (profile >= protocol::profileCount) {
        throw std::invalid_argument("HHKB profile number must be between 0 and 3");
    }

    const auto original = activeProfile();
    if (original == profile) {
        action();
        return;
    }

    const auto returnNote = [original](const std::string& reason) {
        return reason + " The keyboard could not be returned to profile "
            + std::to_string(original + 1) + ".";
    };

    try {
        switchProfile(profile);
        action();
    } catch (const std::exception& error) {
        if (tryActivate(original)) {
            throw;
        }
        throw DeviceError(DeviceErrorCode::InputOutput, returnNote(error.what()));
    }

    try {
        switchProfile(original);
    } catch (const std::exception& error) {
        throw DeviceError(DeviceErrorCode::InputOutput, returnNote(error.what()));
    }
}

bool HhkbStudioDevice::tryActivate(const std::uint16_t profile)
{
    try {
        if (activeProfile() != profile) {
            switchProfile(profile);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool HhkbStudioDevice::padState(const std::size_t pad)
{
    const auto change = protocol::decodePadNotification(
        transport_.exchange(protocol::encodePadStateRequest(pad)));
    if (!change || change->pad != pad) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio answered a gesture pad request unexpectedly");
    }
    return change->on;
}

void HhkbStudioDevice::setPadState(const std::size_t pad, const bool on)
{
    const auto response = transport_.exchange(protocol::encodePadStateWrite(pad, on));
    const bool acknowledged = response[0] == 0x03 && response[1] == 0x11 && response[2] == 0x05
        && response[3] == 0x01 && response[4] == pad && (response[5] != 0) == on;
    if (!acknowledged) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio did not acknowledge the gesture pad change");
    }
    if (padState(pad) != on) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio did not keep the gesture pad change");
    }
}

void HhkbStudioDevice::switchProfile(const std::uint16_t profile)
{
    if (transport_.isBluetooth()) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "Switching profiles needs a USB connection");
    }
    const auto responses = transport_.exchange(
        protocol::encodeProfileSwitchRequest(profile),
        protocol::profileSwitchResponseCount);
    if (responses.size() != protocol::profileSwitchResponseCount) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio answered a profile switch with an unexpected number of reports");
    }
    for (const auto& response : responses) {
        if (protocol::decodeBigEndian16(response, protocol::textPayloadOffset) != profile) {
            throw DeviceError(
                DeviceErrorCode::Protocol,
                "HHKB Studio did not confirm the requested profile");
        }
    }

    if (activeProfile() != profile) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio is not on the requested profile after switching");
    }
}

void HhkbStudioDevice::requireTarget(const std::uint16_t expectedProfile)
{
    if (readProductName() != "HHKB-Studio") {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "The connected device is not an HHKB Studio");
    }
    if (activeProfile() != expectedProfile) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "The keyboard's active profile changed since it was read. "
            "Read from the keyboard again before applying.");
    }
}

void HhkbStudioDevice::writeCurrentProfile(
    const std::vector<std::uint8_t>& profile,
    const std::vector<std::uint8_t>& backup)
{
    if (profile.size() != keymap::Keymap::profileByteCount
        || backup.size() != keymap::Keymap::profileByteCount) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "Profile data has an unexpected length");
    }

    try {
        writeData(0, profile);
    } catch (const std::exception& error) {
        const bool restored = tryRestore(backup);
        throw DeviceError(
            DeviceErrorCode::InputOutput,
            std::string("Writing the profile failed: ") + error.what()
                + (restored
                       ? " The previous profile was restored."
                       : " The previous profile could not be restored; "
                         "apply the saved backup to recover."));
    }

    if (readCurrentProfile() != profile) {
        const bool restored = tryRestore(backup);
        throw DeviceError(
            DeviceErrorCode::Protocol,
            std::string("The keyboard did not keep the written profile. ")
                + (restored
                       ? "The previous profile was restored."
                       : "The previous profile could not be restored; "
                         "apply the saved backup to recover."));
    }
}

bool HhkbStudioDevice::tryRestore(const std::vector<std::uint8_t>& backup)
{
    try {
        writeData(0, backup);
        return readCurrentProfile() == backup;
    } catch (const std::exception&) {
        return false;
    }
}

void HhkbStudioDevice::writeData(
    const std::uint16_t start,
    const std::vector<std::uint8_t>& data)
{
    for (std::size_t offset = 0; offset < data.size();
         offset += protocol::maximumDataPayload) {
        const auto count = std::min<std::size_t>(
            protocol::maximumDataPayload,
            data.size() - offset);
        const std::vector<std::uint8_t> chunk(
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + count));
        // The response layout is not documented, so success is judged by the
        // read-back comparison rather than by the acknowledgement.
        static_cast<void>(transport_.exchange(protocol::encodeDataWriteRequest(
            static_cast<std::uint16_t>(start + offset),
            chunk)));
    }
}

Report HhkbStudioDevice::readProperty(const protocol::Property property)
{
    return transport_.exchange(protocol::encodePropertyRequest(property));
}

std::vector<std::uint8_t> HhkbStudioDevice::readData(
    const std::uint16_t start,
    const std::uint16_t length)
{
    std::vector<std::uint8_t> data;
    data.reserve(length);

    std::uint16_t offset = 0;
    while (offset < length) {
        const auto blockSize = static_cast<std::uint8_t>(
            std::min<std::uint16_t>(
                protocol::maximumDataPayload,
                length - offset));
        const auto address = static_cast<std::uint16_t>(start + offset);
        const auto response = transport_.exchange(
            protocol::encodeDataReadRequest(address, blockSize));
        auto block = protocol::decodeBytes(
            response,
            protocol::dataPayloadOffset,
            blockSize);
        data.insert(data.end(), block.begin(), block.end());
        offset = static_cast<std::uint16_t>(offset + blockSize);
    }

    if (data.size() != length) {
        throw DeviceError(
            DeviceErrorCode::Protocol,
            "HHKB Studio returned an unexpected profile length");
    }
    return data;
}

}  // namespace hhkbs::device
