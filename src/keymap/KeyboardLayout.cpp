#include "keymap/KeyboardLayout.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace hhkbs::keymap {
namespace {

struct KeyDefinition {
    std::size_t slot;
    std::string_view legend;
    float width;
    Keymap::ScanCode scanCode;
};

void setScanCode(
    std::vector<std::uint8_t>& bytes,
    const std::size_t layer,
    const std::size_t slot,
    const Keymap::ScanCode scanCode)
{
    const auto offset = (layer * Keymap::layerByteCount)
        + (slot * Keymap::scanCodeByteCount);
    bytes[offset] = static_cast<std::uint8_t>(scanCode >> 8U);
    bytes[offset + 1] = static_cast<std::uint8_t>(scanCode & 0xFFU);
}

void appendRow(
    std::vector<KeyPosition>& positions,
    const std::span<const KeyDefinition> definitions,
    const float y,
    const float startX = 0.0F)
{
    float x = startX;
    for (const auto& definition : definitions) {
        positions.push_back(KeyPosition{
            .slot = definition.slot,
            .legend = std::string(definition.legend),
            .x = x,
            .y = y,
            .width = definition.width,
            .defaultScanCode = definition.scanCode,
        });
        x += definition.width;
    }
}

std::vector<KeyPosition> makeUsStudioLayout()
{
    std::vector<KeyPosition> positions;
    positions.reserve(63);

    constexpr std::array row0{
        KeyDefinition{0, "Esc", 1.0F, 0x0029},
        KeyDefinition{1, "1", 1.0F, 0x001E},
        KeyDefinition{2, "2", 1.0F, 0x001F},
        KeyDefinition{3, "3", 1.0F, 0x0020},
        KeyDefinition{4, "4", 1.0F, 0x0021},
        KeyDefinition{5, "5", 1.0F, 0x0022},
        KeyDefinition{6, "6", 1.0F, 0x0023},
        KeyDefinition{7, "7", 1.0F, 0x0024},
        KeyDefinition{8, "8", 1.0F, 0x0025},
        KeyDefinition{9, "9", 1.0F, 0x0026},
        KeyDefinition{10, "0", 1.0F, 0x0027},
        KeyDefinition{11, "-", 1.0F, 0x002D},
        KeyDefinition{12, "=", 1.0F, 0x002E},
        KeyDefinition{13, "\\", 1.0F, 0x0031},
        KeyDefinition{14, "`", 1.0F, 0x0035},
    };
    appendRow(positions, row0, 0.0F);

    constexpr std::array row1{
        KeyDefinition{15, "Tab", 1.5F, 0x002B},
        KeyDefinition{16, "Q", 1.0F, 0x0014},
        KeyDefinition{17, "W", 1.0F, 0x001A},
        KeyDefinition{18, "E", 1.0F, 0x0008},
        KeyDefinition{19, "R", 1.0F, 0x0015},
        KeyDefinition{20, "T", 1.0F, 0x0017},
        KeyDefinition{21, "Y", 1.0F, 0x001C},
        KeyDefinition{22, "U", 1.0F, 0x0018},
        KeyDefinition{23, "I", 1.0F, 0x000C},
        KeyDefinition{24, "O", 1.0F, 0x0012},
        KeyDefinition{25, "P", 1.0F, 0x0013},
        KeyDefinition{26, "[", 1.0F, 0x002F},
        KeyDefinition{27, "]", 1.0F, 0x0030},
        KeyDefinition{29, "Delete", 1.5F, 0x004C},
    };
    appendRow(positions, row1, 1.0F);

    constexpr std::array row2{
        KeyDefinition{30, "Control", 1.75F, 0x00E0},
        KeyDefinition{31, "A", 1.0F, 0x0004},
        KeyDefinition{32, "S", 1.0F, 0x0016},
        KeyDefinition{33, "D", 1.0F, 0x0007},
        KeyDefinition{34, "F", 1.0F, 0x0009},
        KeyDefinition{35, "G", 1.0F, 0x000A},
        KeyDefinition{36, "H", 1.0F, 0x000B},
        KeyDefinition{37, "J", 1.0F, 0x000D},
        KeyDefinition{38, "K", 1.0F, 0x000E},
        KeyDefinition{39, "L", 1.0F, 0x000F},
        KeyDefinition{40, ";", 1.0F, 0x0033},
        KeyDefinition{41, "'", 1.0F, 0x0034},
        KeyDefinition{44, "Return", 2.25F, 0x0028},
    };
    appendRow(positions, row2, 2.0F);

    constexpr std::array row3{
        KeyDefinition{45, "Shift", 2.25F, 0x00E1},
        KeyDefinition{46, "Z", 1.0F, 0x001D},
        KeyDefinition{47, "X", 1.0F, 0x001B},
        KeyDefinition{48, "C", 1.0F, 0x0006},
        KeyDefinition{49, "V", 1.0F, 0x0019},
        KeyDefinition{50, "B", 1.0F, 0x0005},
        KeyDefinition{51, "N", 1.0F, 0x0011},
        KeyDefinition{52, "M", 1.0F, 0x0010},
        KeyDefinition{53, ",", 1.0F, 0x0036},
        KeyDefinition{54, ".", 1.0F, 0x0037},
        KeyDefinition{55, "/", 1.0F, 0x0038},
        KeyDefinition{58, "Shift", 1.75F, 0x00E5},
        KeyDefinition{59, "Fn", 1.0F, 0x5101},
    };
    appendRow(positions, row3, 3.0F);

    constexpr std::array row4{
        KeyDefinition{62, "Alt", 1.25F, 0x00E2},
        KeyDefinition{63, "◇", 1.5F, 0x00E3},
        KeyDefinition{65, "Space", 6.25F, 0x002C},
        KeyDefinition{68, "◇", 1.5F, 0x00E7},
        KeyDefinition{69, "Alt", 1.25F, 0x00E6},
    };
    appendRow(positions, row4, 4.0F, 1.625F);

    constexpr std::array mouseButtons{
        KeyDefinition{79, "Mouse L", 1.5F, 0x0000},
        KeyDefinition{80, "Mouse / Fn2", 1.2F, 0x5102},
        KeyDefinition{81, "Mouse R", 1.5F, 0x0000},
    };
    appendRow(positions, mouseButtons, 5.25F, 5.4F);

    return positions;
}

}  // namespace

const std::vector<KeyPosition>& KeyboardLayout::usStudio()
{
    static const auto positions = makeUsStudioLayout();
    return positions;
}

std::vector<std::uint8_t> KeyboardLayout::demoProfile()
{
    return usWindowsFactoryProfile();
}

std::vector<std::uint8_t> KeyboardLayout::usWindowsFactoryProfile()
{
    std::vector<std::uint8_t> bytes(Keymap::profileByteCount, 0);

    for (std::size_t layer = 0; layer < Keymap::layerCount; ++layer) {
        for (const auto& position : usStudio()) {
            setScanCode(bytes, layer, position.slot, position.defaultScanCode);
        }

        setScanCode(bytes, layer, 28, 0x004C);
        setScanCode(bytes, layer, 29, 0x002A);
        setScanCode(bytes, layer, 79, 0x00F4);
        setScanCode(bytes, layer, 80, 0x5102);
        setScanCode(bytes, layer, 81, 0x00F6);
        setScanCode(bytes, layer, 86, 0x0052);
        setScanCode(bytes, layer, 87, 0x0051);
        setScanCode(bytes, layer, 101, 0x0050);
        setScanCode(bytes, layer, 102, 0x004F);
        setScanCode(bytes, layer, 108, 0x5F8C);
        setScanCode(bytes, layer, 109, 0x5F8D);
        setScanCode(bytes, layer, 116, 0x00F9);
        setScanCode(bytes, layer, 117, 0x00FA);
    }

    constexpr std::array fn1Overrides{
        std::pair<std::size_t, Keymap::ScanCode>{0, 0x00A5},
        std::pair<std::size_t, Keymap::ScanCode>{1, 0x003A},
        std::pair<std::size_t, Keymap::ScanCode>{2, 0x003B},
        std::pair<std::size_t, Keymap::ScanCode>{3, 0x003C},
        std::pair<std::size_t, Keymap::ScanCode>{4, 0x003D},
        std::pair<std::size_t, Keymap::ScanCode>{5, 0x003E},
        std::pair<std::size_t, Keymap::ScanCode>{6, 0x003F},
        std::pair<std::size_t, Keymap::ScanCode>{7, 0x0040},
        std::pair<std::size_t, Keymap::ScanCode>{8, 0x0041},
        std::pair<std::size_t, Keymap::ScanCode>{9, 0x0042},
        std::pair<std::size_t, Keymap::ScanCode>{10, 0x0043},
        std::pair<std::size_t, Keymap::ScanCode>{11, 0x0044},
        std::pair<std::size_t, Keymap::ScanCode>{12, 0x0045},
        std::pair<std::size_t, Keymap::ScanCode>{13, 0x0049},
        std::pair<std::size_t, Keymap::ScanCode>{14, 0x004C},
        std::pair<std::size_t, Keymap::ScanCode>{15, 0x0039},
        std::pair<std::size_t, Keymap::ScanCode>{23, 0x0046},
        std::pair<std::size_t, Keymap::ScanCode>{24, 0x0047},
        std::pair<std::size_t, Keymap::ScanCode>{25, 0x0048},
        std::pair<std::size_t, Keymap::ScanCode>{26, 0x0052},
        std::pair<std::size_t, Keymap::ScanCode>{28, 0x002A},
        std::pair<std::size_t, Keymap::ScanCode>{31, 0x00AA},
        std::pair<std::size_t, Keymap::ScanCode>{32, 0x00A9},
        std::pair<std::size_t, Keymap::ScanCode>{33, 0x00A8},
        std::pair<std::size_t, Keymap::ScanCode>{34, 0x00B0},
        std::pair<std::size_t, Keymap::ScanCode>{36, 0x0055},
        std::pair<std::size_t, Keymap::ScanCode>{37, 0x0054},
        std::pair<std::size_t, Keymap::ScanCode>{38, 0x004A},
        std::pair<std::size_t, Keymap::ScanCode>{39, 0x004B},
        std::pair<std::size_t, Keymap::ScanCode>{40, 0x0050},
        std::pair<std::size_t, Keymap::ScanCode>{41, 0x004F},
        std::pair<std::size_t, Keymap::ScanCode>{44, 0x0058},
        std::pair<std::size_t, Keymap::ScanCode>{51, 0x0057},
        std::pair<std::size_t, Keymap::ScanCode>{52, 0x0056},
        std::pair<std::size_t, Keymap::ScanCode>{53, 0x004D},
        std::pair<std::size_t, Keymap::ScanCode>{54, 0x004E},
        std::pair<std::size_t, Keymap::ScanCode>{55, 0x0051},
        std::pair<std::size_t, Keymap::ScanCode>{68, 0x0078},
    };
    for (const auto& [slot, scanCode] : fn1Overrides) {
        setScanCode(bytes, 1, slot, scanCode);
    }

    constexpr std::array fn2Overrides{
        std::pair<std::size_t, Keymap::ScanCode>{1, 0x5FA4},
        std::pair<std::size_t, Keymap::ScanCode>{2, 0x5FA5},
        std::pair<std::size_t, Keymap::ScanCode>{3, 0x5FA6},
        std::pair<std::size_t, Keymap::ScanCode>{4, 0x5FA7},
        std::pair<std::size_t, Keymap::ScanCode>{6, 0x5F9E},
        std::pair<std::size_t, Keymap::ScanCode>{7, 0x5F9F},
        std::pair<std::size_t, Keymap::ScanCode>{8, 0x5FA0},
        std::pair<std::size_t, Keymap::ScanCode>{9, 0x5FA1},
        std::pair<std::size_t, Keymap::ScanCode>{16, 0x005F},
        std::pair<std::size_t, Keymap::ScanCode>{17, 0x0060},
        std::pair<std::size_t, Keymap::ScanCode>{18, 0x0061},
        std::pair<std::size_t, Keymap::ScanCode>{31, 0x005C},
        std::pair<std::size_t, Keymap::ScanCode>{32, 0x005D},
        std::pair<std::size_t, Keymap::ScanCode>{33, 0x005E},
        std::pair<std::size_t, Keymap::ScanCode>{35, 0x5FA2},
        std::pair<std::size_t, Keymap::ScanCode>{36, 0x00F5},
        std::pair<std::size_t, Keymap::ScanCode>{46, 0x0059},
        std::pair<std::size_t, Keymap::ScanCode>{47, 0x005A},
        std::pair<std::size_t, Keymap::ScanCode>{48, 0x005B},
        std::pair<std::size_t, Keymap::ScanCode>{50, 0x5FA3},
        std::pair<std::size_t, Keymap::ScanCode>{65, 0x0062},
    };
    for (const auto& [slot, scanCode] : fn2Overrides) {
        setScanCode(bytes, 2, slot, scanCode);
    }

    return bytes;
}

}  // namespace hhkbs::keymap
