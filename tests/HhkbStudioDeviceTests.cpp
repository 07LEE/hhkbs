#include "device/DeviceDiscovery.h"
#include "device/HhkbProtocol.h"
#include "device/HhkbStudioDevice.h"
#include "device/Transport.h"
#include "keymap/Keymap.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

using hhkbs::device::HhkbStudioDevice;
using hhkbs::device::Report;
using hhkbs::device::Transport;
using hhkbs::keymap::Keymap;

void require(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeText(Report& report, const std::string& text)
{
    for (std::size_t index = 0; index < text.size() && index + 3 < report.size(); ++index) {
        report[index + 3] = static_cast<std::uint8_t>(text[index]);
    }
}

class FakeTransport final : public Transport {
public:
    [[nodiscard]] Report exchange(const Report& request) override
    {
        requests.push_back(request);
        Report response{};

        if (request[0] == 0x12) {
            const auto offset = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(request[1]) << 8U)
                | static_cast<std::uint16_t>(request[2]);
            for (std::uint8_t index = 0; index < request[3]; ++index) {
                response[4 + index] =
                    static_cast<std::uint8_t>((offset + index) & 0xFFU);
            }
            return response;
        }

        const auto command = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(request[1]) << 8U)
            | static_cast<std::uint16_t>(request[2]);
        switch (command) {
        case 0x1001:
            writeText(response, "HHKB-Studio");
            break;
        case 0x1002:
            writeText(response, "US");
            break;
        case 0x1003:
            writeText(response, "1.0");
            break;
        case 0x1005:
            writeText(response, "PD-ID100");
            break;
        case 0x1007:
            writeText(response, "serial");
            break;
        case 0x100B:
            writeText(response, "A2.00");
            break;
        case 0x1101:
            response[3] = 0;
            response[4] = 2;
            break;
        case 0x1103:
            response[3] = 1;
            response[4] = 0;
            response[5] = 1;
            response[6] = 0;
            response[7] = 1;
            response[8] = 0;
            break;
        default:
            break;
        }
        return response;
    }

    std::vector<Report> requests;
};

void informationCommandsAreDecoded()
{
    FakeTransport transport;
    HhkbStudioDevice device(transport);

    const auto information = device.readInformation();

    require(information.productName == "HHKB-Studio", "product name was not decoded");
    require(information.modelName == "PD-ID100", "model name was not decoded");
    require(information.keyboardLayout == "US", "layout was not decoded");
    require(information.firmwareVersion == "A2.00", "firmware was not decoded");
    require(information.currentProfile == 2, "current profile was not decoded");
    require(information.dipSwitches == 0b101010, "DIP switches were not packed");
    require(transport.requests.size() == 8, "unexpected information request count");
    require(
        transport.requests.front()[0] == 0x02
            && transport.requests.front()[1] == 0x10
            && transport.requests.front()[2] == 0x01,
        "product request was encoded incorrectly");
}

void protocolPacketsAreEncodedAndDecoded()
{
    const auto propertyRequest =
        hhkbs::device::protocol::encodePropertyRequest(
            hhkbs::device::protocol::Property::FirmwareVersion);
    require(
        propertyRequest[0] == 0x02
            && propertyRequest[1] == 0x10
            && propertyRequest[2] == 0x0B,
        "property request was encoded incorrectly");

    const auto dataRequest =
        hhkbs::device::protocol::encodeDataReadRequest(0x1234, 26);
    require(
        dataRequest[0] == 0x12
            && dataRequest[1] == 0x12
            && dataRequest[2] == 0x34
            && dataRequest[3] == 26,
        "data request was encoded incorrectly");

    Report response{};
    response[3] = 0xAB;
    response[4] = 0xCD;
    require(
        hhkbs::device::protocol::decodeBigEndian16(response, 3) == 0xABCD,
        "big-endian value was decoded incorrectly");
}

void profileIsReadInBoundedChunks()
{
    FakeTransport transport;
    HhkbStudioDevice device(transport);

    const auto profile = device.readCurrentProfile();

    require(profile.size() == Keymap::profileByteCount, "profile length is incorrect");
    for (std::size_t index = 0; index < profile.size(); ++index) {
        require(
            profile[index] == static_cast<std::uint8_t>(index & 0xFFU),
            "profile chunk was assembled at the wrong offset");
    }

    constexpr std::size_t expectedRequestCount =
        (Keymap::profileByteCount + 25) / 26;
    require(
        transport.requests.size() == expectedRequestCount,
        "profile used an unexpected number of chunk requests");
    require(transport.requests.front()[3] == 26, "full chunk length is incorrect");
    require(transport.requests.back()[3] == 24, "final chunk length is incorrect");
}

void supportedInterfacesAreDiscovered()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("hhkbs-device-discovery-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "hidraw-test-hhkb" / "device");
    std::filesystem::create_directories(root / "hidraw-test-other" / "device");

    {
        std::ofstream uevent(root / "hidraw-test-hhkb" / "device" / "uevent");
        uevent << "HID_ID=0003:000004FE:00000016\n"
               << "HID_NAME=HHKB-Studio\n";
    }
    {
        std::ofstream uevent(root / "hidraw-test-other" / "device" / "uevent");
        uevent << "HID_ID=0003:00001234:00005678\n"
               << "HID_NAME=Other keyboard\n";
    }

    const auto interfaces =
        hhkbs::device::DeviceDiscovery::findHhkbStudioInterfaces(root);
    std::filesystem::remove_all(root);

    require(interfaces.size() == 1, "device discovery did not filter by USB ID");
    require(interfaces.front().name == "HHKB-Studio", "device name was not parsed");
    require(
        interfaces.front().vendorId == hhkbs::device::hhkbStudioVendorId,
        "vendor ID was not parsed");
    require(
        interfaces.front().productId == hhkbs::device::hhkbStudioProductId,
        "product ID was not parsed");
    require(
        interfaces.front().path == "/dev/hidraw-test-hhkb",
        "hidraw device path was assembled incorrectly");
}

}  // namespace

int main()
{
    try {
        informationCommandsAreDecoded();
        protocolPacketsAreEncodedAndDecoded();
        profileIsReadInBoundedChunks();
        supportedInterfacesAreDiscovered();
    } catch (const std::exception& error) {
        std::cerr << "HHKB device test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All HHKB device tests passed\n";
    return 0;
}
