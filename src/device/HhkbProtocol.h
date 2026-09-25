#pragma once

#include "device/Transport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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

// A gesture pad's state. The keyboard sends it by itself when a pad is switched, and answers a request for it
// in the same layout: 02 11 05 01 <pad> <state>.
struct PadNotification {
    std::size_t pad{};  // 0 left side, 1 front left, 2 front right, 3 right side
    bool on{};
};

inline constexpr std::size_t gesturePadCount = 4;
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
// Asks for one gesture pad's state (0-3), and switches it on or off.
[[nodiscard]] Report encodePadStateRequest(std::size_t pad);
[[nodiscard]] Report encodePadStateWrite(std::size_t pad, bool on);
[[nodiscard]] Report encodeDataWriteRequest(
    std::uint16_t address,
    const std::vector<std::uint8_t>& data);

// True for a report with the header of a pad state, which is also the header of the answer to a pad state request.
[[nodiscard]] bool isNotification(const Report& report);
[[nodiscard]] std::optional<PadNotification> decodePadNotification(const Report& report);

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
