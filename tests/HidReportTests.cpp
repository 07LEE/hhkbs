#include "device/HidDescriptor.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using hhkbs::device::Report;
using hhkbs::device::configurationReportId;
using hhkbs::device::extractConfigurationReport;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

using Bytes = std::vector<std::uint8_t>;

// Usage Page 0x01 (generic desktop), Usage keyboard, Collection, Report ID 1: the ordinary key reports.
const Bytes keyboardCollection{0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
                               0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0};

// Usage Page 0xFF60 (vendor), a collection of 32-byte reports, with the Report ID given or without one.
Bytes vendorCollection(const std::vector<std::uint8_t>& reportId)
{
    Bytes bytes{0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01};
    bytes.insert(bytes.end(), reportId.begin(), reportId.end());
    const Bytes rest{0x09, 0x62, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x20, 0x81, 0x02, 0xC0};
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    return bytes;
}

Bytes joined(const Bytes& first, const Bytes& second)
{
    Bytes bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());
    return bytes;
}

void usbHasNoReportId()
{
    require(!configurationReportId(vendorCollection({})), "a USB descriptor has no Report ID on the configuration reports");
    require(!configurationReportId(joined(keyboardCollection, vendorCollection({}))),
            "the Report ID of the key reports is not the configuration one");
}

void bluetoothUsesReportIdFour()
{
    require(configurationReportId(vendorCollection({0x85, 0x04})) == 4, "the Report ID of the vendor collection was not found");
    require(configurationReportId(joined(keyboardCollection, vendorCollection({0x85, 0x04}))) == 4,
            "the key reports hide the configuration Report ID");
    require(configurationReportId(joined(vendorCollection({0x85, 0x04}), keyboardCollection)) == 4,
            "a collection after the vendor one replaced its Report ID");
}

void otherPagesAreIgnored()
{
    Bytes other{0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x09, 0xC0};
    require(!configurationReportId(other), "a vendor page other than 0xFF60 was taken for the configuration one");
}

void unusualItemsAreSkippedCorrectly()
{
    // A four-byte item (Logical Minimum) and a long item (0xFE) must be stepped over by their real size.
    Bytes odd{0x17, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x02, 0x10, 0xAA, 0xBB};
    require(configurationReportId(joined(odd, vendorCollection({0x85, 0x04}))) == 4,
            "an item of an unusual size threw the parsing off");
}

void brokenDescriptorsAreSafe()
{
    require(!configurationReportId({}), "an empty descriptor has no Report ID");
    const Bytes cut{0x06, 0x60};  // the item says two bytes of data, one is there
    require(!configurationReportId(cut), "a cut-off item was read");
    const Bytes cutLong{0xFE};
    require(!configurationReportId(cutLong), "a cut-off long item was read");
    auto whole = vendorCollection({0x85, 0x04});
    whole.resize(whole.size() - 10);
    static_cast<void>(configurationReportId(whole));  // must not read past the end
}

void usbPacketsAreTheReportItself()
{
    Report report{};
    Bytes packet(32);
    for (std::size_t index = 0; index < packet.size(); ++index) packet[index] = static_cast<std::uint8_t>(index + 1);

    require(extractConfigurationReport(packet, std::nullopt, report), "a full USB packet was refused");
    require(report[0] == 1 && report[31] == 32, "the report was not copied as it is");

    Bytes shortPacket(31);
    Report untouched{};
    require(!extractConfigurationReport(shortPacket, std::nullopt, untouched), "a short packet was accepted");
    require(untouched == Report{}, "a refused packet changed the report");
    require(!extractConfigurationReport({}, std::nullopt, untouched), "an empty packet was accepted");
}

void bluetoothPacketsNeedTheirReportId()
{
    Report report{};
    Bytes packet(33);
    packet[0] = 4;
    for (std::size_t index = 1; index < packet.size(); ++index) packet[index] = static_cast<std::uint8_t>(index);

    require(extractConfigurationReport(packet, std::uint8_t{4}, report), "a Bluetooth configuration packet was refused");
    require(report[0] == 1 && report[31] == 32, "the Report ID byte was copied into the report");

    packet[0] = 1;  // a key report on the same node
    Report other{};
    require(!extractConfigurationReport(packet, std::uint8_t{4}, other), "a key report was taken for a configuration one");
    require(other == Report{}, "a refused packet changed the report");

    Bytes tooShort(32);
    tooShort[0] = 4;
    require(!extractConfigurationReport(tooShort, std::uint8_t{4}, other), "a packet without room for the Report ID was accepted");
    require(extractConfigurationReport(Bytes(40, 4), std::uint8_t{4}, other), "a longer packet should still give its first report");
}

}  // namespace

int main()
{
    try {
        usbHasNoReportId();
        bluetoothUsesReportIdFour();
        otherPagesAreIgnored();
        unusualItemsAreSkippedCorrectly();
        brokenDescriptorsAreSafe();
        usbPacketsAreTheReportItself();
        bluetoothPacketsNeedTheirReportId();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
