#include "device/PadMonitor.h"

#include "device/HidDescriptor.h"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace hhkbs::device {

PadMonitor::~PadMonitor() { stop(); }

void PadMonitor::start(const std::filesystem::path& path)
{
    if (thread_.joinable() && running_ && path_ == path) return;
    stop();
    path_ = path;
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this, path] { run(path); });
}

void PadMonitor::stop()
{
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    path_.clear();
}

PadMonitor::State PadMonitor::state(const std::size_t pad) const
{
    return pad < states_.size() ? static_cast<State>(states_[pad].load()) : State::Unknown;
}

void PadMonitor::set(const std::size_t pad, const bool on)
{
    if (pad < states_.size()) states_[pad] = on ? 1 : 0;
}

void PadMonitor::run(const std::filesystem::path& path)
{
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        running_ = false;
        return;
    }

    const auto reportId = readConfigurationReportId(descriptor);
    while (!stop_) {
        pollfd item{.fd = descriptor, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&item, 1, 200);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        if ((item.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) break;

        std::array<std::uint8_t, Report{}.size() + 1> packet{};
        const auto count = ::read(descriptor, packet.data(), packet.size());
        Report report{};
        if (count <= 0 || !extractConfigurationReport({packet.data(), static_cast<std::size_t>(count)}, reportId, report)) continue;
        if (const auto change = protocol::decodePadNotification(report)) {
            states_[change->pad] = change->on ? 1 : 0;
        }
    }
    ::close(descriptor);
    // Unplugged or stopped: what the pads were doing is no longer known.
    for (auto& state : states_) state = -1;
    running_ = false;
}

}  // namespace hhkbs::device
