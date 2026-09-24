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

    // Makes `profile` (0-3) the active profile, then confirms both answers and a
    // fresh read of the active profile agree.
    void switchProfile(std::uint16_t profile);

    // Throws unless this is an HHKB Studio whose current profile is expectedProfile.
    void requireTarget(std::uint16_t expectedProfile);

    // Writes the current profile, reads it back and compares. If the write fails
    // or the read-back differs, `backup` is written back on a best-effort basis
    // before the error is thrown.
    void writeCurrentProfile(
        const std::vector<std::uint8_t>& profile,
        const std::vector<std::uint8_t>& backup);

private:
    [[nodiscard]] Report readProperty(protocol::Property property);
    [[nodiscard]] std::vector<std::uint8_t> readData(
        std::uint16_t start,
        std::uint16_t length);
    void writeData(std::uint16_t start, const std::vector<std::uint8_t>& data);
    [[nodiscard]] bool tryRestore(const std::vector<std::uint8_t>& backup);

    Transport& transport_;
};

}  // namespace hhkbs::device
