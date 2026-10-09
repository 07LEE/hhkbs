#pragma once
// A fake keyboard shared by the tests: it keeps its four profiles, so writes can be read back.
#include "device/DeviceError.h"
#include "device/Transport.h"
#include "keymap/Keymap.h"

#include <array>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

namespace hhkbs::test {

using hhkbs::device::Report;
using hhkbs::device::Transport;
using hhkbs::keymap::Keymap;

inline void writeText(Report& report, const std::string& text)
{
    for (std::size_t index = 0; index < text.size() && index + 3 < report.size(); ++index) {
        report[index + 3] = static_cast<std::uint8_t>(text[index]);
    }
}

// Behaves like a keyboard that stores its profile, so writes can be read back.
class StorageTransport final : public Transport {
public:
    // Each profile holds its own data; `memory` is the active one.
    StorageTransport()
        : memory(Keymap::profileByteCount)
    {
        for (std::size_t profile = 0; profile < stored.size(); ++profile) {
            stored[profile].assign(Keymap::profileByteCount, 0);
            for (std::size_t index = 0; index < Keymap::profileByteCount; ++index) {
                stored[profile][index] =
                    static_cast<std::uint8_t>(index * 7U + profile * 31U);
            }
        }
        memory = stored.at(currentProfile);
    }

    using Transport::exchange;

    // Mirrors the keyboard: a profile switch is answered by two reports.
    [[nodiscard]] std::vector<Report> exchange(
        const Report& request,
        const std::size_t responseCount) override
    {
        if (request[0] != 0x03) {
            return Transport::exchange(request, responseCount);
        }
        ++switches;
        const std::uint8_t requested = request[4];
        const bool ignored = ignoreSwitch
            || (ignoreSwitchNumber != 0 && switches == ignoreSwitchNumber);
        if (!ignored) {
            stored.at(currentProfile) = memory;
            currentProfile = requested;
            memory = stored.at(currentProfile);
        }
        Report notification{};
        notification[0] = 0x02;
        notification[1] = 0x11;
        notification[2] = 0x01;
        notification[4] = ignored ? currentProfile : requested;
        notification[7] = 0x20;
        Report acknowledgement = notification;
        acknowledgement[0] = 0x03;
        acknowledgement[7] = 0;
        std::vector<Report> responses{notification, acknowledgement};
        responses.resize(switchResponseCount, acknowledgement);
        return responses;
    }

    [[nodiscard]] Report exchange(const Report& request) override
    {
        if (beforeRequest) beforeRequest(*this, request);
        Report response{};
        const auto address = static_cast<std::size_t>(
            (static_cast<std::uint16_t>(request[1]) << 8U) | request[2]);

        if (request[0] == 0x12) {
            ++dataReads;
            if (writes > 0 && (failReadsAfterWrite || failNextReadAfterWrite)) {
                failNextReadAfterWrite = false;
                throw hhkbs::device::DeviceError(hhkbs::device::DeviceErrorCode::Timeout, "timed out");
            }
            for (std::size_t index = 0; index < request[3]; ++index) {
                response[4 + index] = memory.at(address + index);
            }
        } else if (request[0] == 0x13) {
            ++writes;
            if (failWriteNumber != 0 && writes == failWriteNumber) {
                throw hhkbs::device::DeviceError(
                    hhkbs::device::DeviceErrorCode::Disconnected,
                    "unplugged");
            }
            for (std::size_t index = 0; index < request[3]; ++index) {
                const auto value = request[4 + index];
                // The first `corruptUntilWrite` chunk writes are stored wrongly; later ones are kept as sent.
                memory.at(address + index) =
                    writes <= corruptUntilWrite ? static_cast<std::uint8_t>(~value) : value;
            }
        } else if (address == 0x1001) {
            writeText(response, productName);
        } else if (address == 0x1007) {
            writeText(response, serialNumber);
        } else if (address == 0x1101) {
            response[4] = currentProfile;
        }
        return response;
    }

    std::vector<std::uint8_t> memory;
    std::array<std::vector<std::uint8_t>, 4> stored;
    std::string productName = "HHKB-Studio";
    std::string serialNumber = "serial";
    std::uint8_t currentProfile = 2;
    std::size_t writes = 0;
    std::size_t switches = 0;
    std::size_t switchResponseCount = 2;
    bool ignoreSwitch = false;
    std::size_t ignoreSwitchNumber = 0;
    std::size_t failWriteNumber = 0;
    std::size_t corruptUntilWrite = 0;
    std::size_t dataReads = 0;
    bool failNextReadAfterWrite = false;  // the first read after a write times out, once
    bool failReadsAfterWrite = false;     // every read after a write times out
    // Called with every request before it is answered, so a test can change the keyboard at a chosen moment.
    std::function<void(StorageTransport&, const Report&)> beforeRequest;
};

inline std::vector<std::uint8_t> patternProfile(const std::uint8_t seed)
{
    std::vector<std::uint8_t> profile(Keymap::profileByteCount);
    for (std::size_t index = 0; index < profile.size(); ++index) {
        profile[index] = static_cast<std::uint8_t>(index + seed);
    }
    return profile;
}

}  // namespace hhkbs::test
