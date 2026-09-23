#pragma once
#include "keymap/Keymap.h"
#include <filesystem>

namespace hhkbs::keymap {
// Save through a sibling temporary file so a failed write never truncates a profile.
[[nodiscard]] Keymap readProfile(const std::filesystem::path& path);
void writeProfile(const std::filesystem::path& path, const Keymap& keymap, bool overwrite);
}
