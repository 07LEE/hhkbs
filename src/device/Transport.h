#pragma once

#include "device/DeviceError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hhkbs::device {

using Report = std::array<std::uint8_t, 32>;

class Transport {
public:
    virtual ~Transport() = default;

    // Over Bluetooth the keyboard counts its profiles from 1, and a request to switch profiles has not been checked
    // there. USB counts from 0.
    [[nodiscard]] virtual bool numbersProfilesFromOne() const { return false; }
    [[nodiscard]] virtual Report exchange(const Report& request) = 0;

    // Some commands answer with more than one report. Every answer must be read,
    // or the next request would receive a stale one.
    [[nodiscard]] virtual std::vector<Report> exchange(
        const Report& request,
        const std::size_t responseCount)
    {
        if (responseCount != 1) {
            throw DeviceError(
                DeviceErrorCode::Protocol,
                "This transport cannot read multiple responses");
        }
        return {exchange(request)};
    }
};

}  // namespace hhkbs::device
