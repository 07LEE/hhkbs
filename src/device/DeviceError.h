#pragma once

#include <stdexcept>
#include <string>

namespace hhkbs::device {

enum class DeviceErrorCode {
    PermissionDenied,
    OpenFailed,
    Disconnected,
    Timeout,
    InputOutput,
    Protocol,
};

class DeviceError final : public std::runtime_error {
public:
    DeviceError(const DeviceErrorCode code, const std::string& message)
        : std::runtime_error(message)
        , code_(code)
    {
    }

    [[nodiscard]] DeviceErrorCode code() const noexcept
    {
        return code_;
    }

private:
    DeviceErrorCode code_;
};

}  // namespace hhkbs::device
