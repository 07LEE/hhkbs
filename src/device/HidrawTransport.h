#pragma once

#include "device/Transport.h"

#include <chrono>
#include <filesystem>

namespace hhkbs::device {

class HidrawTransport final : public Transport {
public:
    explicit HidrawTransport(
        const std::filesystem::path& path,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(750));
    ~HidrawTransport() override;

    HidrawTransport(const HidrawTransport&) = delete;
    HidrawTransport& operator=(const HidrawTransport&) = delete;
    HidrawTransport(HidrawTransport&&) = delete;
    HidrawTransport& operator=(HidrawTransport&&) = delete;

    [[nodiscard]] Report exchange(const Report& request) override;

private:
    void waitFor(short events) const;
    void writeReport(const Report& report) const;
    [[nodiscard]] Report readReport() const;

    int fileDescriptor_{-1};
    std::filesystem::path path_;
    std::chrono::milliseconds timeout_;
};

}  // namespace hhkbs::device
