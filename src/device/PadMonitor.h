#pragma once

#include "device/HhkbProtocol.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>

namespace hhkbs::device {

// Keeps the last known state of the gesture pads, and follows the keyboard reporting a pad switched on or off
// (for example from a key press) while it is listening. A pad is Unknown until it has been read or reported.
class PadMonitor final {
public:
    enum class State : std::int8_t { Unknown = -1, Off = 0, On = 1 };

    PadMonitor() = default;
    ~PadMonitor();
    PadMonitor(const PadMonitor&) = delete;
    PadMonitor& operator=(const PadMonitor&) = delete;

    // Starts listening on a configuration interface; does nothing if it is already listening on that path.
    void start(const std::filesystem::path& path);
    void stop();
    // False once the listener has ended, which happens when the keyboard is unplugged or the interface fails.
    [[nodiscard]] bool listening() const { return running_; }
    [[nodiscard]] State state(std::size_t pad) const;
    // Records a state read from the keyboard, or one this program just set.
    void set(std::size_t pad, bool on);

private:
    void run(const std::filesystem::path& path);

    std::thread thread_;
    std::filesystem::path path_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::array<std::atomic<std::int8_t>, protocol::gesturePadCount> states_{-1, -1, -1, -1};
};

}  // namespace hhkbs::device
