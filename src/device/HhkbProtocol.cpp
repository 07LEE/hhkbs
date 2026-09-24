#include "device/HhkbProtocol.h"

#include <algorithm>
#include <stdexcept>

namespace hhkbs::device::protocol {
namespace {

void requireRange(
    const std::size_t offset,
    const std::size_t count,
    const std::size_t size)
{
    if (offset > size || count > size - offset) {
        throw std::out_of_range("HHKB report payload is outside the report boundary");
    }
}

}  // namespace

Report encodePropertyRequest(const Property property)
{
    const auto command = static_cast<std::uint16_t>(property);
    Report report{};
    report[0] = 0x02;
    report[1] = static_cast<std::uint8_t>(command >> 8U);
    report[2] = static_cast<std::uint8_t>(command & 0xFFU);
    return report;
}

Report encodeDataReadRequest(
    const std::uint16_t address,
    const std::uint8_t length)
{
    if (length == 0 || length > maximumDataPayload) {
        throw std::invalid_argument("HHKB data request length must be between 1 and 26");
    }

    Report report{};
    report[0] = 0x12;
    report[1] = static_cast<std::uint8_t>(address >> 8U);
    report[2] = static_cast<std::uint8_t>(address & 0xFFU);
    report[3] = length;
    return report;
}

Report encodePadStateRequest(const std::size_t pad)
{
    if (pad >= gesturePadCount) {
        throw std::invalid_argument("HHKB gesture pad must be between 0 and 3");
    }
    Report report{};
    report[0] = 0x02;
    report[1] = 0x11;
    report[2] = 0x05;
    report[3] = 0x01;
    report[4] = static_cast<std::uint8_t>(pad);
    return report;
}

Report encodePadStateWrite(const std::size_t pad, const bool on)
{
    auto report = encodePadStateRequest(pad);
    report[0] = 0x03;
    report[5] = on ? 1 : 0;
    return report;
}

Report encodeProfileSwitchRequest(const std::uint16_t profile)
{
    if (profile >= profileCount) {
        throw std::invalid_argument("HHKB profile number must be between 0 and 3");
    }

    const auto command = static_cast<std::uint16_t>(Property::CurrentProfile);
    Report report{};
    report[0] = 0x03;
    report[1] = static_cast<std::uint8_t>(command >> 8U);
    report[2] = static_cast<std::uint8_t>(command & 0xFFU);
    report[3] = static_cast<std::uint8_t>(profile >> 8U);
    report[4] = static_cast<std::uint8_t>(profile & 0xFFU);
    return report;
}

Report encodeDataWriteRequest(
    const std::uint16_t address,
    const std::vector<std::uint8_t>& data)
{
    if (data.empty() || data.size() > maximumDataPayload) {
        throw std::invalid_argument("HHKB data write length must be between 1 and 26");
    }

    Report report{};
    report[0] = 0x13;
    report[1] = static_cast<std::uint8_t>(address >> 8U);
    report[2] = static_cast<std::uint8_t>(address & 0xFFU);
    report[3] = static_cast<std::uint8_t>(data.size());
    std::copy(data.begin(), data.end(), report.begin() + dataPayloadOffset);
    return report;
}

bool isNotification(const Report& report)
{
    return report[0] == 0x02 && report[1] == 0x11 && report[2] == 0x05;
}

std::optional<PadNotification> decodePadNotification(const Report& report)
{
    // 02 11 05 01 <pad> <state>: the 01 marks a gesture pad; other kinds are not understood.
    if (!isNotification(report) || report[3] != 0x01 || report[4] >= gesturePadCount || report[5] > 1) {
        return std::nullopt;
    }
    return PadNotification{report[4], report[5] == 1};
}

std::string decodeText(const Report& report, const std::size_t offset)
{
    requireRange(offset, 0, report.size());

    std::string text;
    for (std::size_t index = offset;
         index < report.size() && report[index] != 0;
         ++index) {
        const auto character = report[index];
        if (character >= 0x20 && character <= 0x7E) {
            text.push_back(static_cast<char>(character));
        }
    }
    return text;
}

std::uint16_t decodeBigEndian16(
    const Report& report,
    const std::size_t offset)
{
    requireRange(offset, 2, report.size());
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(report[offset]) << 8U)
        | static_cast<std::uint16_t>(report[offset + 1]);
}

std::uint8_t decodeBitBytes(
    const Report& report,
    const std::size_t offset,
    const std::size_t count)
{
    requireRange(offset, count, report.size());

    std::uint8_t bits = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        bits = static_cast<std::uint8_t>((bits << 1U) | (report[index] & 1U));
    }
    return bits;
}

std::vector<std::uint8_t> decodeBytes(
    const Report& report,
    const std::size_t offset,
    const std::size_t count)
{
    requireRange(offset, count, report.size());
    return {
        report.begin() + static_cast<std::ptrdiff_t>(offset),
        report.begin() + static_cast<std::ptrdiff_t>(offset + count),
    };
}

}  // namespace hhkbs::device::protocol
