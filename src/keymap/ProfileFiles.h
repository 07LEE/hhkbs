#pragma once
#include "keymap/Keymap.h"
#include <filesystem>

namespace hhkbs::keymap {
// The largest file that is read as a profile. A real one is a few kilobytes.
inline constexpr std::size_t maxProfileFileBytes = 1024 * 1024;

// Reads a profile file. Throws std::runtime_error, with a message fit to show, for a file that cannot be read, is not a
// regular file, is too large, or is not a profile.
[[nodiscard]] Keymap readProfile(const std::filesystem::path& path);

// Saves through a sibling temporary file, so a failed write never truncates a profile. With `overwrite` false a file
// that is already there is refused. Throws std::system_error.
void writeProfile(const std::filesystem::path& path, const Keymap& keymap, bool overwrite);
}
