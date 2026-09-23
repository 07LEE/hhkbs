#pragma once

#include "keymap/Keymap.h"

#include <string>
#include <string_view>

namespace hhkbs::keymap {

class ProfileSerializer final {
public:
    [[nodiscard]] static std::string toToml(const Keymap& keymap);
    [[nodiscard]] static Keymap fromToml(std::string_view document);
};

}  // namespace hhkbs::keymap
