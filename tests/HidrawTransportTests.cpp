#include "device/DeviceError.h"
#include "device/HhkbProtocol.h"
#include "device/HidrawTransport.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace {

using hhkbs::device::DeviceError;
using hhkbs::device::DeviceErrorCode;
using hhkbs::device::HidrawTransport;
using hhkbs::device::Report;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// A terminal in raw mode stands in for the device node: what the program writes can be read from the master side,
// and what the test writes to the master side is what the keyboard "sent".
class FakeNode {
public:
    FakeNode()
    {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        require(master_ >= 0 && ::grantpt(master_) == 0 && ::unlockpt(master_) == 0, "could not create the fake device");
        path_ = ::ptsname(master_);
        slave_ = ::open(path_.c_str(), O_RDWR | O_NOCTTY);
        require(slave_ >= 0, "could not open the fake device");
        termios settings{};
        require(::tcgetattr(slave_, &settings) == 0, "could not read the terminal settings");
        ::cfmakeraw(&settings);
        require(::tcsetattr(slave_, TCSANOW, &settings) == 0, "could not set the terminal settings");
    }
    ~FakeNode()
    {
        if (slave_ >= 0) ::close(slave_);
        if (master_ >= 0) ::close(master_);
    }
    FakeNode(const FakeNode&) = delete;
    FakeNode& operator=(const FakeNode&) = delete;

    [[nodiscard]] const std::string& path() const { return path_; }

    // What the keyboard sends by itself, or as an answer.
    void send(const Report& report) const
    {
        require(::write(master_, report.data(), report.size()) == static_cast<ssize_t>(report.size()), "write failed");
    }

    // Waits for the program to write a request. Returns false if none comes.
    [[nodiscard]] bool receive(Report& request, const int milliseconds) const
    {
        pollfd item{.fd = master_, .events = POLLIN, .revents = 0};
        if (::poll(&item, 1, milliseconds) <= 0) return false;
        return ::read(master_, request.data(), request.size()) == static_cast<ssize_t>(request.size());
    }

private:
    int master_{-1};
    int slave_{-1};
    std::string path_;
};

Report nameRequest() { return hhkbs::device::protocol::encodePropertyRequest(hhkbs::device::protocol::Property::ProductName); }

Report answer(const char* text)
{
    Report report{0x02, 0x10, 0x01};
    for (std::size_t index = 0; text[index] != '\0'; ++index) report[3 + index] = static_cast<std::uint8_t>(text[index]);
    return report;
}

void anAnswerComesBack()
{
    FakeNode node;
    HidrawTransport transport(node.path(), std::chrono::milliseconds(500));
    std::thread keyboard([&] {
        Report request{};
        if (node.receive(request, 2000)) node.send(answer("HHKB"));
    });
    const auto response = transport.exchange(nameRequest());
    keyboard.join();
    require(response[3] == 'H' && response[6] == 'B', "the answer did not come back as it was sent");
}

void reportsThatAreNotTheAnswerDoNotExtendTheWait()
{
    FakeNode node;
    HidrawTransport transport(node.path(), std::chrono::milliseconds(300));
    std::atomic<bool> stop{false};
    std::thread keyboard([&] {
        Report request{};
        if (!node.receive(request, 2000)) return;
        // Pad changes keep arriving, none of them the answer; each one used to start the wait over.
        for (int sent = 0; sent < 25 && !stop; ++sent) {  // about 1.5 s of them, so a regression ends instead of hanging
            node.send(Report{0x02, 0x11, 0x05, 0x01, 0x01, 0x00});
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    });

    const auto started = std::chrono::steady_clock::now();
    bool timedOut = false;
    try {
        static_cast<void>(transport.exchange(nameRequest()));
    } catch (const DeviceError& error) {
        timedOut = error.code() == DeviceErrorCode::Timeout;
    }
    const auto waited = std::chrono::steady_clock::now() - started;
    stop = true;
    keyboard.join();

    require(timedOut, "an exchange with no answer did not time out");
    require(waited < std::chrono::milliseconds(900), "the wait went on while other reports kept arriving");
}

void aLateAnswerIsNotTakenForTheNextOne()
{
    FakeNode node;
    HidrawTransport transport(node.path(), std::chrono::milliseconds(200));

    bool timedOut = false;
    try {
        static_cast<void>(transport.exchange(nameRequest()));  // nobody answers
    } catch (const DeviceError& error) {
        timedOut = error.code() == DeviceErrorCode::Timeout;
    }
    require(timedOut, "an exchange with no answer did not time out");

    node.send(answer("late"));  // the answer to the request that gave up arrives after all
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread keyboard([&] {
        Report request{};
        if (node.receive(request, 2000)) node.send(answer("new"));
    });
    const auto response = transport.exchange(nameRequest());
    keyboard.join();

    require(response[3] == 'n' && response[4] == 'e' && response[5] == 'w', "the late answer was taken for the answer to a later request");
}

void reportsFromBeforeTheRequestAreLeftOut()
{
    FakeNode node;
    HidrawTransport transport(node.path(), std::chrono::milliseconds(500));
    node.send(answer("old"));
    node.send(Report{0x02, 0x11, 0x05, 0x01, 0x02, 0x01});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread keyboard([&] {
        Report request{};
        if (node.receive(request, 2000)) node.send(answer("new"));
    });
    const auto response = transport.exchange(nameRequest());
    keyboard.join();

    require(response[3] == 'n', "something the keyboard sent before the request was taken for its answer");
}

void aDeviceThatIsGoneIsNoticed()
{
    auto node = std::make_unique<FakeNode>();
    HidrawTransport transport(node->path(), std::chrono::milliseconds(300));
    node.reset();  // the keyboard is unplugged

    bool failed = false;
    try {
        static_cast<void>(transport.exchange(nameRequest()));
    } catch (const DeviceError&) {
        failed = true;
    }
    require(failed, "an exchange with a device that is gone did not fail");
}

}  // namespace

int main()
{
    try {
        anAnswerComesBack();
        reportsThatAreNotTheAnswerDoNotExtendTheWait();
        aLateAnswerIsNotTakenForTheNextOne();
        reportsFromBeforeTheRequestAreLeftOut();
        aDeviceThatIsGoneIsNoticed();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
