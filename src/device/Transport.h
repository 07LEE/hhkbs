#pragma once

#include <array>
#include <cstdint>

namespace hhkbs::device {

using Report = std::array<std::uint8_t, 32>;

class Transport {
public:
    virtual ~Transport() = default;
    [[nodiscard]] virtual Report exchange(const Report& request) = 0;
};

}  // namespace hhkbs::device
