#include "keymap/ProfileSerializer.h"

#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace hhkbs::keymap {
namespace {

std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

Keymap::ScanCode parseScanCode(std::string_view token)
{
    token = trim(token);
    const auto comment = token.find('#');
    if (comment != std::string_view::npos) {
        token = trim(token.substr(0, comment));
    }
    if (token.empty()) {
        throw std::invalid_argument("profile contains an empty scan code");
    }

    int base = 10;
    if (token.starts_with("0x") || token.starts_with("0X")) {
        token.remove_prefix(2);
        base = 16;
    }

    std::uint32_t value{};
    const auto result = std::from_chars(
        token.data(),
        token.data() + token.size(),
        value,
        base);
    if (result.ec != std::errc{}
        || result.ptr != token.data() + token.size()
        || value > 0xFFFFU) {
        throw std::invalid_argument("profile contains an invalid scan code");
    }
    return static_cast<Keymap::ScanCode>(value);
}

}  // namespace

std::string ProfileSerializer::toToml(const Keymap& keymap)
{
    std::string document;
    document.reserve(7000);

    for (std::size_t layer = 0; layer < Keymap::layerCount; ++layer) {
        document += "[[layers]]\nscancodes = [\n";
        for (std::size_t key = 0; key < Keymap::keysPerLayer; ++key) {
            if (key % 15 == 0) {
                document += "  ";
            }
            document += std::format("0x{:04x},", keymap.scanCode(layer, key));
            document += key % 15 == 14 ? "\n" : " ";
        }
        document += "]\n";
        if (layer + 1 < Keymap::layerCount) {
            document += "\n";
        }
    }
    return document;
}

Keymap ProfileSerializer::fromToml(const std::string_view document)
{
    constexpr std::string_view layerHeader = "[[layers]]";
    std::vector<std::uint8_t> bytes;
    bytes.reserve(Keymap::profileByteCount);

    std::size_t searchOffset = 0;
    std::size_t layerCount = 0;
    while (true) {
        const auto layerStart = document.find(layerHeader, searchOffset);
        if (layerStart == std::string_view::npos) {
            break;
        }
        ++layerCount;

        const auto nextLayer =
            document.find(layerHeader, layerStart + layerHeader.size());
        const auto layerEnd =
            nextLayer == std::string_view::npos ? document.size() : nextLayer;
        const auto assignment = document.find("scancodes", layerStart);
        if (assignment == std::string_view::npos || assignment >= layerEnd) {
            throw std::invalid_argument("profile layer has no scancodes array");
        }
        const auto arrayStart = document.find('[', assignment);
        const auto arrayEnd =
            arrayStart == std::string_view::npos
                ? std::string_view::npos
                : document.find(']', arrayStart + 1);
        if (arrayStart == std::string_view::npos
            || arrayEnd == std::string_view::npos
            || arrayEnd >= layerEnd) {
            throw std::invalid_argument("profile layer has an invalid scancodes array");
        }

        std::size_t count = 0;
        std::size_t tokenStart = arrayStart + 1;
        while (tokenStart < arrayEnd) {
            const auto comma = document.find(',', tokenStart);
            const auto tokenEnd =
                comma == std::string_view::npos || comma > arrayEnd
                    ? arrayEnd
                    : comma;
            const auto token = trim(document.substr(tokenStart, tokenEnd - tokenStart));
            if (!token.empty()) {
                const auto code = parseScanCode(token);
                bytes.push_back(static_cast<std::uint8_t>(code >> 8U));
                bytes.push_back(static_cast<std::uint8_t>(code & 0xFFU));
                ++count;
            }
            if (tokenEnd == arrayEnd) {
                break;
            }
            tokenStart = tokenEnd + 1;
        }

        if (count != Keymap::keysPerLayer) {
            throw std::invalid_argument(
                "each profile layer must contain exactly 120 scan codes");
        }
        searchOffset = layerEnd;
    }

    if (layerCount != Keymap::layerCount) {
        throw std::invalid_argument("profile must contain exactly four layers");
    }
    return Keymap(bytes);
}

}  // namespace hhkbs::keymap
