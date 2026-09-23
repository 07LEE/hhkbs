#include "keymap/KeyboardLayout.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace hhkbs::keymap {
namespace {

struct KeyDefinition {
    std::size_t slot;
    std::string_view legend;
    float width;
    Keymap::ScanCode scanCode;
};

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
    std::vector<std::uint8_t> bytes(Keymap::profileByteCount, 0);
    for (const auto& position : usStudio()) {
        const auto offset = position.slot * Keymap::scanCodeByteCount;
        bytes[offset] = static_cast<std::uint8_t>(position.defaultScanCode >> 8U);
        bytes[offset + 1] =
            static_cast<std::uint8_t>(position.defaultScanCode & 0xFFU);
    }
    return bytes;
}

}  // namespace hhkbs::keymap
