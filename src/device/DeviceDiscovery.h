#pragma once

#include "device/DeviceInfo.h"

#include <filesystem>
#include <vector>

namespace hhkbs::device {

class DeviceDiscovery final {
public:
    [[nodiscard]] static std::vector<DeviceInfo> findHhkbStudioInterfaces(
        const std::filesystem::path& hidrawClassPath = "/sys/class/hidraw");
};

}  // namespace hhkbs::device
