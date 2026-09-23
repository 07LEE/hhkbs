#pragma once

#include "keymap/Keymap.h"

#include <cstddef>
#include <string>
#include <vector>

namespace hhkbs::keymap {

struct KeyPosition {
    std::size_t slot{};
    std::string legend;
    float x{};
    float y{};
    float width{1.0F};
    Keymap::ScanCode defaultScanCode{};
};

class KeyboardLayout final {
public:
    [[nodiscard]] static const std::vector<KeyPosition>& usStudio();
    [[nodiscard]] static std::vector<std::uint8_t> demoProfile();
};

}  // namespace hhkbs::keymap
