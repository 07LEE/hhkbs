#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace hhkbs::device {

struct DeviceInfo {
    std::filesystem::path path;
    std::string name;
    std::uint16_t vendorId{};
    std::uint16_t productId{};
    bool canReadWrite{};
};

inline constexpr std::uint16_t hhkbStudioVendorId = 0x04FE;
inline constexpr std::uint16_t hhkbStudioProductId = 0x0016;

}  // namespace hhkbs::device
