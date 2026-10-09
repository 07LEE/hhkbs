#include "device/HhkbProtocol.h"
#include "device/PadMonitor.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

using hhkbs::device::PadMonitor;
using hhkbs::device::Report;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool eventually(const std::function<bool()>& condition)
{
    for (int attempt = 0; attempt < 150; ++attempt) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// A fake device node that is removed again whatever happens in the test.
class Fifo {
public:
    explicit Fifo(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / ("hhkbs-" + name + "-" + std::to_string(::getpid())))
    {
        std::filesystem::remove(path_);
        if (::mkfifo(path_.c_str(), 0600) != 0) throw std::runtime_error("could not create the fake device");
    }
    ~Fifo()
    {
        closeWriter();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    Fifo(const Fifo&) = delete;
    Fifo& operator=(const Fifo&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    // Waits for the monitor to open the other end, without ever blocking the test if it does not.
    void openWriter()
    {
        for (int attempt = 0; attempt < 150 && writer_ < 0; ++attempt) {
            writer_ = ::open(path_.c_str(), O_WRONLY | O_NONBLOCK);
            if (writer_ < 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(writer_ >= 0, "the monitor never opened the device");
    }
    void send(const hhkbs::device::Report& report) const
    {
        require(::write(writer_, report.data(), report.size()) == static_cast<ssize_t>(report.size()), "write failed");
    }
    void send(const std::string& bytes) const
    {
        require(::write(writer_, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()), "write failed");
    }
    void closeWriter()
    {
        if (writer_ >= 0) ::close(writer_);
        writer_ = -1;
    }

private:
    std::filesystem::path path_;
    int writer_{-1};
};

void followsNotifications()
{
    Fifo fifo("pad-follow");
    PadMonitor monitor;
    require(monitor.state(1) == PadMonitor::State::Unknown, "a pad must be unknown before any report");
    monitor.start(fifo.path());
    fifo.openWriter();

    fifo.send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x01});
    require(eventually([&] { return monitor.state(1) == PadMonitor::State::On; }), "front left should be on after its report");
    fifo.send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x00});
    require(eventually([&] { return monitor.state(1) == PadMonitor::State::Off; }), "front left should be off after its report");
    require(monitor.state(0) == PadMonitor::State::Unknown, "a pad that never reported stays unknown");

    monitor.stop();
    require(monitor.state(1) == PadMonitor::State::Unknown, "pads are unknown once listening stops");
    require(!monitor.listening(), "a stopped monitor still says it is listening");
}

void ignoresWhatIsNotAPadReport()
{
    Fifo fifo("pad-noise");
    PadMonitor monitor;
    monitor.start(fifo.path());
    fifo.openWriter();

    // A hidraw node hands over one report per read, but a fifo is a plain byte stream: each report is given time to
    // be read before the next is written, so they cannot run together.
    const auto sendAlone = [&](const auto& packet) {
        fifo.send(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };
    sendAlone(std::string(8, '\x01'));                          // too short for a configuration report
    sendAlone(Report{0x01, 0x00, 0x04});                        // a packet of another kind
    sendAlone(Report{0x02, 0x11, 0x01, 0x01, 0x02});            // a profile notification, not a pad
    sendAlone(Report{0x02, 0x11, 0x05, 0x01, 0x07, 0x01});      // a pad that does not exist
    fifo.send(Report{0x02, 0x11, 0x05, 0x01, 0x02, 0x01});      // the one real change, last
    require(eventually([&] { return monitor.state(2) == PadMonitor::State::On; }), "the pad report after the noise was lost");
    require(monitor.state(0) == PadMonitor::State::Unknown && monitor.state(1) == PadMonitor::State::Unknown
                && monitor.state(3) == PadMonitor::State::Unknown,
            "noise changed a pad");
    require(monitor.listening(), "noise ended the listening");
}

void endsWhenTheDeviceGoesAway()
{
    Fifo fifo("pad-gone");
    PadMonitor monitor;
    monitor.start(fifo.path());
    fifo.openWriter();
    fifo.send(Report{0x02, 0x11, 0x05, 0x01, 0x00, 0x01});
    require(eventually([&] { return monitor.state(0) == PadMonitor::State::On; }), "the pad report was not followed");
    require(monitor.listening(), "the monitor should be listening");

    fifo.closeWriter();  // the keyboard is unplugged
    require(eventually([&] { return !monitor.listening(); }), "the monitor kept listening to a device that went away");
    require(monitor.state(0) == PadMonitor::State::Unknown, "pad states were kept after the keyboard went away");
}

void aDeviceThatCannotBeOpenedIsNotListened()
{
    PadMonitor monitor;
    monitor.start("/nonexistent/hidraw");
    require(eventually([&] { return !monitor.listening(); }), "a device that cannot be opened counted as listening");
    monitor.stop();
}

void listeningMovesToAnotherDevice()
{
    Fifo first("pad-first");
    Fifo second("pad-second");
    PadMonitor monitor;
    monitor.start(first.path());
    first.openWriter();
    first.send(Report{0x02, 0x11, 0x05, 0x01, 0x03, 0x01});
    require(eventually([&] { return monitor.state(3) == PadMonitor::State::On; }), "the first device was not followed");

    monitor.start(second.path());
    require(monitor.state(3) == PadMonitor::State::Unknown, "pads of the previous device were kept");
    second.openWriter();
    second.send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x01});
    require(eventually([&] { return monitor.state(1) == PadMonitor::State::On; }), "the second device was not followed");
    require(monitor.listening(), "the monitor should be listening to the second device");
}

}  // namespace

int main()
{
    try {
        followsNotifications();
        ignoresWhatIsNotAPadReport();
        endsWhenTheDeviceGoesAway();
        aDeviceThatCannotBeOpenedIsNotListened();
        listeningMovesToAnotherDevice();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
