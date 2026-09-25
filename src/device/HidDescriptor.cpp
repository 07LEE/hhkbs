#include "device/HidDescriptor.h"

#include <algorithm>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <vector>

namespace hhkbs::device {
namespace {

constexpr std::uint32_t configurationUsagePage = 0xFF60;

}  // namespace

std::optional<std::uint8_t> configurationReportId(const std::span<const std::uint8_t> descriptor)
{
    std::uint32_t usagePage = 0;
    std::optional<std::uint8_t> reportId;
    for (std::size_t position = 0; position < descriptor.size();) {
        const std::uint8_t prefix = descriptor[position];
        if (prefix == 0xFE) {  // a long item: <prefix> <data size> <tag> <data>
            if (position + 1 >= descriptor.size()) break;
            position += 3 + static_cast<std::size_t>(descriptor[position + 1]);
            continue;
        }
        const std::size_t size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
        if (position + 1 + size > descriptor.size()) break;
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < size; ++index) {
            value |= static_cast<std::uint32_t>(descriptor[position + 1 + index]) << (8 * index);
        }
        const bool global = ((prefix >> 2) & 0x03) == 1;
        const auto tag = static_cast<std::uint8_t>(prefix >> 4);
        if (global && tag == 0x0) usagePage = value;
        else if (global && tag == 0x8 && usagePage == configurationUsagePage) reportId = static_cast<std::uint8_t>(value);
        position += 1 + size;
    }
    return reportId;
}

std::optional<std::uint8_t> readConfigurationReportId(const int hidraw)
{
    int size = 0;
    if (::ioctl(hidraw, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0 || size > HID_MAX_DESCRIPTOR_SIZE) {
        return std::nullopt;
    }
    hidraw_report_descriptor descriptor{};
    descriptor.size = static_cast<__u32>(size);
    if (::ioctl(hidraw, HIDIOCGRDESC, &descriptor) < 0) return std::nullopt;
    return configurationReportId({descriptor.value, descriptor.size});
}

bool extractConfigurationReport(
    const std::span<const std::uint8_t> packet,
    const std::optional<std::uint8_t> reportId,
    Report& report)
{
    const std::size_t prefix = reportId ? 1 : 0;
    if (packet.size() < prefix + report.size()) return false;
    if (reportId && packet[0] != *reportId) return false;
    std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(prefix), report.size(), report.begin());
    return true;
}

}  // namespace hhkbs::device
