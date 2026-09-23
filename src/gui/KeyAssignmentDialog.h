#pragma once
#include "keymap/Keymap.h"
#include <array>
#include <optional>

class KeyAssignmentDialog {
public:
    void reset(hhkbs::keymap::Keymap::ScanCode code);
    std::optional<hhkbs::keymap::Keymap::ScanCode> draw();
private:
    std::array<char, 128> search_{};
    std::array<char, 16> raw_{};
};
