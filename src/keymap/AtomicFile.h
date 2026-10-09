#pragma once
#include <filesystem>
#include <string_view>

namespace hhkbs::keymap {

// Writes `data` to a new file next to `path`, flushes it, and puts it at `path` in one step, so a write that fails or
// is cut short never leaves `path` half written, and what was at `path` before is kept until the new file is whole.
// The new file can be read and written by its owner only. With `overwrite` false an existing file at `path`, a
// dangling symbolic link included, is refused instead of replaced. Throws std::system_error.
void writeFileAtomically(const std::filesystem::path& path, std::string_view data, bool overwrite);

}  // namespace hhkbs::keymap
