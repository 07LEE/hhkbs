#include "device/DeviceDiscovery.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace hhkbs::device {
namespace {

struct UeventData {
    std::string name;
    std::uint16_t bus{};
    std::uint16_t vendorId{};
    std::uint16_t productId{};
    bool hasHidId{};
};

std::optional<std::uint32_t> parseHex(const std::string_view value)
{
    std::uint32_t parsed{};
    const auto result = std::from_chars(
        value.data(),
        value.data() + value.size(),
        parsed,
        16);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

void parseHidId(const std::string_view value, UeventData& data)
{
    const auto firstSeparator = value.find(':');
    const auto secondSeparator =
        firstSeparator == std::string_view::npos
            ? std::string_view::npos
            : value.find(':', firstSeparator + 1);
    if (firstSeparator == std::string_view::npos
        || secondSeparator == std::string_view::npos) {
        return;
    }

    const auto bus = parseHex(value.substr(0, firstSeparator));
    const auto vendor = parseHex(
        value.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1));
    const auto product = parseHex(value.substr(secondSeparator + 1));
    if (!bus || !vendor || !product || *bus > 0xFFFFU || *vendor > 0xFFFFU || *product > 0xFFFFU) {
        return;
    }

    data.bus = static_cast<std::uint16_t>(*bus);
    data.vendorId = static_cast<std::uint16_t>(*vendor);
    data.productId = static_cast<std::uint16_t>(*product);
    data.hasHidId = true;
}

std::optional<UeventData> readUevent(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    UeventData data;
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view hidIdPrefix = "HID_ID=";
        constexpr std::string_view hidNamePrefix = "HID_NAME=";
        if (line.starts_with(hidIdPrefix)) {
            parseHidId(std::string_view(line).substr(hidIdPrefix.size()), data);
        } else if (line.starts_with(hidNamePrefix)) {
            data.name = line.substr(hidNamePrefix.size());
        }
    }

    return data.hasHidId ? std::optional<UeventData>(std::move(data)) : std::nullopt;
}

}  // namespace

std::vector<DeviceInfo> DeviceDiscovery::findHhkbStudioInterfaces(
    const std::filesystem::path& hidrawClassPath)
{
    std::vector<DeviceInfo> devices;
    std::error_code error;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator iterator(hidrawClassPath, error);
         !error && iterator != end;
         iterator.increment(error)) {
        const auto uevent = readUevent(iterator->path() / "device" / "uevent");
        if (!uevent
            || uevent->vendorId != hhkbStudioVendorId
            || uevent->productId != hhkbStudioProductId) {
            continue;
        }

        const auto devicePath =
            std::filesystem::path("/dev") / iterator->path().filename();
        devices.push_back(DeviceInfo{
            .path = devicePath,
            .name = uevent->name,
            .vendorId = uevent->vendorId,
            .productId = uevent->productId,
            .canReadWrite = ::access(devicePath.c_str(), R_OK | W_OK) == 0,
            .bluetooth = uevent->bus == busBluetooth,
        });
    }

    std::ranges::sort(
        devices,
        {},
        [](const DeviceInfo& device) { return device.path.string(); });
    return devices;
}

}  // namespace hhkbs::device
