#include "device/HhkbStudioDevice.h"

#include "device/DeviceError.h"
#include "keymap/Keymap.h"

#include <algorithm>
#include <exception>
#include <limits>

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
    information.currentProfile = protocol::decodeBigEndian16(
        readProperty(protocol::Property::CurrentProfile),
        protocol::textPayloadOffset);
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

void HhkbStudioDevice::switchProfile(const std::uint16_t profile)
{
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

    const auto active = protocol::decodeBigEndian16(
        readProperty(protocol::Property::CurrentProfile),
        protocol::textPayloadOffset);
    if (active != profile) {
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
    const auto current = protocol::decodeBigEndian16(
        readProperty(protocol::Property::CurrentProfile),
        protocol::textPayloadOffset);
    if (current != expectedProfile) {
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
