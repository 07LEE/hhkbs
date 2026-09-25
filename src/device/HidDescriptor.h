#pragma once

#include "device/Transport.h"

#include <cstdint>
#include <optional>
#include <span>

namespace hhkbs::device {

// The Report ID of the vendor-defined configuration collection (usage page 0xFF60), or nothing when that
// collection has none. Over USB the configuration reports carry no ID; over Bluetooth they use ID 4.
[[nodiscard]] std::optional<std::uint8_t> configurationReportId(std::span<const std::uint8_t> descriptor);

// The same for an open hidraw node; nothing when its descriptor cannot be read.
[[nodiscard]] std::optional<std::uint8_t> readConfigurationReportId(int hidraw);

// Takes one packet returned by a single read() and copies the configuration report out of it. Returns false for
// anything else, such as the key and mouse reports a Bluetooth node delivers on the same descriptor.
[[nodiscard]] bool extractConfigurationReport(
    std::span<const std::uint8_t> packet,
    std::optional<std::uint8_t> reportId,
    Report& report);

}  // namespace hhkbs::device
