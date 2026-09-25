#include "device/HidrawTransport.h"

#include "device/DeviceError.h"
#include "device/HhkbProtocol.h"
#include "device/HidDescriptor.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace hhkbs::device {
namespace {

DeviceError makeIoError(
    const DeviceErrorCode code,
    const std::filesystem::path& path,
    const std::string& operation)
{
    return DeviceError(
        code,
        operation + " " + path.string() + ": " + std::strerror(errno));
}

}  // namespace

HidrawTransport::HidrawTransport(
    const std::filesystem::path& path,
    const std::chrono::milliseconds timeout)
    : path_(path)
    , timeout_(timeout)
{
    fileDescriptor_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fileDescriptor_ >= 0) {
        reportId_ = readConfigurationReportId(fileDescriptor_);
        return;
    }

    const auto code = errno == EACCES || errno == EPERM
        ? DeviceErrorCode::PermissionDenied
        : DeviceErrorCode::OpenFailed;
    throw makeIoError(code, path_, "could not open");
}

HidrawTransport::~HidrawTransport()
{
    if (fileDescriptor_ >= 0) {
        ::close(fileDescriptor_);
    }
}

Report HidrawTransport::exchange(const Report& request)
{
    writeReport(request);
    return readResponse(request);
}

std::vector<Report> HidrawTransport::exchange(
    const Report& request,
    const std::size_t responseCount)
{
    writeReport(request);
    std::vector<Report> responses;
    responses.reserve(responseCount);
    for (std::size_t index = 0; index < responseCount; ++index) {
        responses.push_back(readResponse(request));
    }
    return responses;
}

Report HidrawTransport::readResponse(const Report& request) const
{
    const bool asksForPad = protocol::isNotification(request) && request[0] == 0x02;
    for (;;) {
        auto report = readReport();
        if (!protocol::isNotification(report)) {
            return report;
        }
        // The answer to a pad request looks like an unsolicited report; it is the one for the pad asked about.
        if (asksForPad && report[3] == request[3] && report[4] == request[4]) {
            return report;
        }
    }
}

void HidrawTransport::waitFor(const short events) const
{
    pollfd descriptor{
        .fd = fileDescriptor_,
        .events = events,
        .revents = 0,
    };

    const auto result = ::poll(&descriptor, 1, static_cast<int>(timeout_.count()));
    if (result == 0) {
        throw DeviceError(
            DeviceErrorCode::Timeout,
            "timed out while communicating with " + path_.string());
    }
    if (result < 0) {
        throw makeIoError(DeviceErrorCode::InputOutput, path_, "could not poll");
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw DeviceError(
            DeviceErrorCode::Disconnected,
            "device disconnected while communicating with " + path_.string());
    }
}

void HidrawTransport::writeReport(const Report& report) const
{
    std::vector<std::uint8_t> packet;
    if (reportId_) packet.push_back(*reportId_);
    packet.insert(packet.end(), report.begin(), report.end());

    std::size_t offset = 0;
    while (offset < packet.size()) {
        waitFor(POLLOUT);
        const auto written = ::write(
            fileDescriptor_,
            packet.data() + offset,
            packet.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        throw makeIoError(DeviceErrorCode::InputOutput, path_, "could not write to");
    }
}

Report HidrawTransport::readReport() const
{
    // One read() returns one whole report. Over Bluetooth the same node also delivers key and mouse reports,
    // which are skipped.
    std::array<std::uint8_t, Report{}.size() + 1> packet{};
    for (;;) {
        waitFor(POLLIN);
        const auto count = ::read(fileDescriptor_, packet.data(), packet.size());
        if (count > 0) {
            Report report{};
            if (extractConfigurationReport({packet.data(), static_cast<std::size_t>(count)}, reportId_, report)) {
                return report;
            }
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        if (count == 0) {
            throw DeviceError(
                DeviceErrorCode::Disconnected,
                "device disconnected while reading from " + path_.string());
        }
        throw makeIoError(DeviceErrorCode::InputOutput, path_, "could not read from");
    }
}

}  // namespace hhkbs::device
