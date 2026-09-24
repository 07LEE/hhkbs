#pragma once

#include "device/Transport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hhkbs::device::protocol {

enum class Property : std::uint16_t {
    ProductName = 0x1001,
    KeyboardLayout = 0x1002,
    BootLoaderVersion = 0x1003,
    ModelName = 0x1005,
    SerialNumber = 0x1007,
    FirmwareVersion = 0x100B,
    CurrentProfile = 0x1101,
    DipSwitches = 0x1103,
};

inline constexpr std::size_t textPayloadOffset = 3;
inline constexpr std::size_t dataPayloadOffset = 4;
inline constexpr std::uint8_t maximumDataPayload = 26;
inline constexpr std::uint16_t profileCount = 4;
inline constexpr std::size_t profileSwitchResponseCount = 2;

[[nodiscard]] Report encodePropertyRequest(Property property);
[[nodiscard]] Report encodeDataReadRequest(
    std::uint16_t address,
    std::uint8_t length);
[[nodiscard]] Report encodeProfileSwitchRequest(std::uint16_t profile);
[[nodiscard]] Report encodeDataWriteRequest(
    std::uint16_t address,
    const std::vector<std::uint8_t>& data);

[[nodiscard]] std::string decodeText(
    const Report& report,
    std::size_t offset = textPayloadOffset);
[[nodiscard]] std::uint16_t decodeBigEndian16(
    const Report& report,
    std::size_t offset);
[[nodiscard]] std::uint8_t decodeBitBytes(
    const Report& report,
    std::size_t offset,
    std::size_t count);
[[nodiscard]] std::vector<std::uint8_t> decodeBytes(
    const Report& report,
    std::size_t offset,
    std::size_t count);

}  // namespace hhkbs::device::protocol
