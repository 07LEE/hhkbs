#pragma once

#include "device/Transport.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

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
    [[nodiscard]] std::vector<Report> exchange(
        const Report& request,
        std::size_t responseCount) override;

private:
    void waitFor(short events) const;
    void writeReport(const Report& report) const;
    [[nodiscard]] Report readReport() const;
    // The next report that answers `request`; reports the keyboard sends on its own are skipped.
    [[nodiscard]] Report readResponse(const Report& request) const;

    int fileDescriptor_{-1};
    std::optional<std::uint8_t> reportId_;  // set when the configuration reports carry a Report ID (Bluetooth)
    std::filesystem::path path_;
    std::chrono::milliseconds timeout_;
};

}  // namespace hhkbs::device
