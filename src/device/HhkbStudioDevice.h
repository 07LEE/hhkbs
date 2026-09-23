#pragma once

#include "device/HhkbProtocol.h"
#include "device/Transport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hhkbs::device {

struct KeyboardInformation {
    std::string productName;
    std::string modelName;
    std::string serialNumber;
    std::string keyboardLayout;
    std::string bootLoaderVersion;
    std::string firmwareVersion;
    std::uint8_t dipSwitches{};
    std::uint16_t currentProfile{};
};

class HhkbStudioDevice final {
public:
    explicit HhkbStudioDevice(Transport& transport);

    [[nodiscard]] std::string readProductName();
    [[nodiscard]] KeyboardInformation readInformation();
    [[nodiscard]] std::vector<std::uint8_t> readCurrentProfile();

private:
    [[nodiscard]] Report readProperty(protocol::Property property);
    [[nodiscard]] std::vector<std::uint8_t> readData(
        std::uint16_t start,
        std::uint16_t length);

    Transport& transport_;
};

}  // namespace hhkbs::device
