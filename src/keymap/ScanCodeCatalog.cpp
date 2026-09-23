#include "keymap/ScanCodeCatalog.h"

#include <array>
#include <format>
#include <string_view>

namespace hhkbs::keymap {
namespace {

std::vector<ScanCodeEntry> makeEntries()
{
    std::vector<ScanCodeEntry> entries{
        {0x0000, "Disabled", "General"},
    };

    for (std::uint16_t index = 0; index < 26; ++index) {
        entries.push_back(ScanCodeEntry{
            .code = static_cast<Keymap::ScanCode>(0x0004 + index),
            .label = std::string(1, static_cast<char>('A' + index)),
            .category = "Letters",
        });
    }

    constexpr std::array digitLabels{
        std::string_view{"1"}, std::string_view{"2"}, std::string_view{"3"},
        std::string_view{"4"}, std::string_view{"5"}, std::string_view{"6"},
        std::string_view{"7"}, std::string_view{"8"}, std::string_view{"9"},
        std::string_view{"0"},
    };
    for (std::size_t index = 0; index < digitLabels.size(); ++index) {
        entries.push_back(ScanCodeEntry{
            .code = static_cast<Keymap::ScanCode>(0x001E + index),
            .label = std::string(digitLabels[index]),
            .category = "Numbers",
        });
    }

    const std::array<ScanCodeEntry, 18> common{{
        {0x0028, "Enter", "Editing"},
        {0x0029, "Escape", "Editing"},
        {0x002A, "Backspace", "Editing"},
        {0x002B, "Tab", "Editing"},
        {0x002C, "Space", "Editing"},
        {0x002D, "Minus", "Symbols"},
        {0x002E, "Equal", "Symbols"},
        {0x002F, "Left Bracket", "Symbols"},
        {0x0030, "Right Bracket", "Symbols"},
        {0x0031, "Backslash", "Symbols"},
        {0x0032, "Non-US Hash", "Symbols"},
        {0x0033, "Semicolon", "Symbols"},
        {0x0034, "Apostrophe", "Symbols"},
        {0x0035, "Grave", "Symbols"},
        {0x0036, "Comma", "Symbols"},
        {0x0037, "Period", "Symbols"},
        {0x0038, "Slash", "Symbols"},
        {0x0039, "Caps Lock", "Editing"},
    }};
    entries.insert(entries.end(), common.begin(), common.end());

    for (std::uint16_t index = 0; index < 12; ++index) {
        entries.push_back(ScanCodeEntry{
            .code = static_cast<Keymap::ScanCode>(0x003A + index),
            .label = "F" + std::to_string(index + 1),
            .category = "Function",
        });
    }

    const std::array<ScanCodeEntry, 13> navigation{{
        {0x0046, "Print Screen", "Navigation"},
        {0x0047, "Scroll Lock", "Navigation"},
        {0x0048, "Pause", "Navigation"},
        {0x0049, "Insert", "Navigation"},
        {0x004A, "Home", "Navigation"},
        {0x004B, "Page Up", "Navigation"},
        {0x004C, "Delete", "Navigation"},
        {0x004D, "End", "Navigation"},
        {0x004E, "Page Down", "Navigation"},
        {0x004F, "Right Arrow", "Navigation"},
        {0x0050, "Left Arrow", "Navigation"},
        {0x0051, "Down Arrow", "Navigation"},
        {0x0052, "Up Arrow", "Navigation"},
    }};
    entries.insert(entries.end(), navigation.begin(), navigation.end());

    for (std::uint16_t index = 0; index < 12; ++index) {
        entries.push_back(ScanCodeEntry{
            .code = static_cast<Keymap::ScanCode>(0x0068 + index),
            .label = "F" + std::to_string(index + 13),
            .category = "Function",
        });
    }

    const std::array<ScanCodeEntry, 17> keypad{{
        {0x0053, "Num Lock", "Keypad"},
        {0x0054, "Keypad Slash", "Keypad"},
        {0x0055, "Keypad Asterisk", "Keypad"},
        {0x0056, "Keypad Minus", "Keypad"},
        {0x0057, "Keypad Plus", "Keypad"},
        {0x0058, "Keypad Enter", "Keypad"},
        {0x0059, "Keypad 1", "Keypad"},
        {0x005A, "Keypad 2", "Keypad"},
        {0x005B, "Keypad 3", "Keypad"},
        {0x005C, "Keypad 4", "Keypad"},
        {0x005D, "Keypad 5", "Keypad"},
        {0x005E, "Keypad 6", "Keypad"},
        {0x005F, "Keypad 7", "Keypad"},
        {0x0060, "Keypad 8", "Keypad"},
        {0x0061, "Keypad 9", "Keypad"},
        {0x0062, "Keypad 0", "Keypad"},
        {0x0063, "Keypad Period", "Keypad"},
    }};
    entries.insert(entries.end(), keypad.begin(), keypad.end());

    const std::array<ScanCodeEntry, 16> studioFunctions{{
        {0x0078, "Stop", "Media"},
        {0x00A5, "Power", "Media"},
        {0x00A8, "Mute", "Media"},
        {0x00A9, "Volume Up", "Media"},
        {0x00AA, "Volume Down", "Media"},
        {0x00B0, "Eject", "Media"},
        {0x00F4, "Mouse Left Click", "Mouse"},
        {0x00F5, "Mouse Wheel Click", "Mouse"},
        {0x00F6, "Mouse Right Click", "Mouse"},
        {0x5F9E, "Gesture Sensitivity Low", "HHKB Studio"},
        {0x5F9F, "Gesture Sensitivity Medium", "HHKB Studio"},
        {0x5FA0, "Gesture Sensitivity High", "HHKB Studio"},
        {0x5FA1, "Gesture Sensitivity Highest", "HHKB Studio"},
        {0x5FA2, "Pointer Speed Increase", "HHKB Studio"},
        {0x5FA3, "Pointer Speed Decrease", "HHKB Studio"},
        {0x5FA4, "Pointer Speed 1", "HHKB Studio"},
    }};
    entries.insert(entries.end(), studioFunctions.begin(), studioFunctions.end());
    entries.push_back({0x5FA5, "Pointer Speed 2", "HHKB Studio"});
    entries.push_back({0x5FA6, "Pointer Speed 3", "HHKB Studio"});
    entries.push_back({0x5FA7, "Pointer Speed 4", "HHKB Studio"});

    const std::array<ScanCodeEntry, 11> modifiers{{
        {0x00E0, "Left Control", "Modifiers"},
        {0x00E1, "Left Shift", "Modifiers"},
        {0x00E2, "Left Alt", "Modifiers"},
        {0x00E3, "Left GUI", "Modifiers"},
        {0x00E4, "Right Control", "Modifiers"},
        {0x00E5, "Right Shift", "Modifiers"},
        {0x00E6, "Right Alt", "Modifiers"},
        {0x00E7, "Right GUI", "Modifiers"},
        {0x5101, "Fn1", "HHKB Studio"},
        {0x5102, "Fn2", "HHKB Studio"},
        {0x5103, "Fn3", "HHKB Studio"},
    }};
    entries.insert(entries.end(), modifiers.begin(), modifiers.end());
    return entries;
}

}  // namespace

const std::vector<ScanCodeEntry>& ScanCodeCatalog::entries()
{
    static const auto catalog = makeEntries();
    return catalog;
}

std::string ScanCodeCatalog::labelFor(const Keymap::ScanCode code)
{
    for (const auto& entry : entries()) {
        if (entry.code == code) {
            return entry.label;
        }
    }
    return std::format("0x{:04X}", code);
}

}  // namespace hhkbs::keymap
