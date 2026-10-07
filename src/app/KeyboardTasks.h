#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The work done with the keyboard on a background thread: it takes what it needs and returns what it found, and
// touches no window state. Each call opens the keyboard itself.
namespace hhkbs::app {

struct ScanResult {
    std::string status;
    std::string detail;
    std::vector<std::uint8_t> bytes;
    std::optional<std::uint16_t> profile;
    std::filesystem::path path;  // the configuration interface that answered
    bool bluetooth = false;      // that interface is a Bluetooth connection
    std::array<std::optional<bool>, 4> pads;  // gesture pad states read from the keyboard
    std::string serial;          // the serial number of the keyboard that was read
};

struct PadResult {
    bool ok = false;
    std::size_t pad = 0;
    bool on = true;
    std::string message;
};

struct ApplyResult {
    bool ok = false;
    std::string message;
    std::vector<std::uint16_t> written;               // the profiles that were written, in order
    std::array<std::vector<std::uint8_t>, 4> bytes;   // what each of them holds now
};

struct ProfileRead {
    std::uint16_t profile = 0;
    std::vector<std::uint8_t> bytes;
    std::string error;  // empty when the profile was read
};

struct PreviewResult { std::vector<ProfileRead> profiles; };

// Looks for a keyboard. With `reconnect` it only finds the connection again; otherwise it reads `target` (or the
// keyboard's current profile). With a `serial`, only the keyboard that carries it is read.
[[nodiscard]] ScanResult scanKeyboard(std::optional<std::uint16_t> target, bool reconnect, const std::string& serial);

[[nodiscard]] PadResult changePad(std::size_t pad, bool on, const std::string& serial);

// Writes each profile (0-3) of `jobs` to the keyboard with `serial`, over USB, keeping a backup of what it held.
[[nodiscard]] ApplyResult applyProfiles(const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>>& jobs,
                                        const std::string& serial);

// Reads the given profiles from the keyboard with `serial`, over USB.
[[nodiscard]] PreviewResult readProfiles(const std::vector<std::uint16_t>& profiles, const std::string& serial);

}  // namespace hhkbs::app
