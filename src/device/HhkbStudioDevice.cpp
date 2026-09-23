#include "device/HhkbStudioDevice.h"

#include "device/DeviceError.h"
#include "keymap/Keymap.h"

#include <algorithm>
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
