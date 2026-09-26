#pragma once

#include "keymap/Keymap.h"

#include <cstddef>
#include <vector>

namespace hhkbs::keymap {

struct KeyChange {
    std::size_t layer{};
    std::size_t slot{};
    Keymap::ScanCode before{};
    Keymap::ScanCode after{};
};

// The slots whose scan code differs between two profiles, in layer then slot order.
[[nodiscard]] std::vector<KeyChange> diffProfiles(const Keymap::Layers& before, const Keymap::Layers& after);

}  // namespace hhkbs::keymap
