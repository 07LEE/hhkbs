#pragma once
#include "keymap/Keymap.h"
#include <array>
#include <optional>
#include <string>

class KeyAssignmentDialog {
public:
    // keyName says which key is being edited; code is what it currently sends.
    void reset(hhkbs::keymap::Keymap::ScanCode code, std::string keyName);
    // Returns the chosen code when the user assigns; sets cancelled when the user backs out.
    std::optional<hhkbs::keymap::Keymap::ScanCode> draw(bool& cancelled);
private:
    std::array<char, 128> search_{};
    std::array<char, 16> raw_{};
    std::string keyName_;
    hhkbs::keymap::Keymap::ScanCode current_ = 0;
    bool scrollToCurrent_ = false;
};
