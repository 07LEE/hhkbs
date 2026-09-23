#pragma once

#include "keymap/Keymap.h"

#include <string>
#include <vector>

namespace hhkbs::keymap {

struct ScanCodeEntry {
    Keymap::ScanCode code{};
    std::string label;
    std::string category;
};

class ScanCodeCatalog final {
public:
    [[nodiscard]] static const std::vector<ScanCodeEntry>& entries();
    [[nodiscard]] static std::string labelFor(Keymap::ScanCode code);
};

}  // namespace hhkbs::keymap
