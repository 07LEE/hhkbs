#pragma once

#include "device/HhkbProtocol.h"
#include "device/Transport.h"

#include <cstdint>
#include <functional>
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

    [[nodiscard]] std::uint16_t activeProfile();

    // Reads profile 0-3 whether or not it is the active one, and leaves the
    // keyboard on the profile that was active before.
    [[nodiscard]] std::vector<std::uint8_t> readProfile(std::uint16_t profile);

    // Runs `action` while `profile` (0-3) is active, then returns the keyboard to
    // the profile that was active before, even when `action` throws. If the
    // keyboard cannot be returned, the thrown error says so.
    void runOnProfile(std::uint16_t profile, const std::function<void()>& action);

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
    [[nodiscard]] bool tryActivate(std::uint16_t profile);
    [[nodiscard]] bool tryRestore(const std::vector<std::uint8_t>& backup);

    Transport& transport_;
};

}  // namespace hhkbs::device
