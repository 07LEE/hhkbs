#include "keymap/AtomicFile.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace hhkbs::keymap {
namespace {

[[noreturn]] void fail(const char* what, const int error = errno)
{
    throw std::system_error(error, std::generic_category(), what);
}

// Puts the finished file at `path` without replacing anything. Renaming with RENAME_NOREPLACE needs nothing from the
// file system but that; a hard link is the older way and is tried where the rename is not supported.
void putInPlaceWithoutReplacing(const char* temporary, const std::filesystem::path& path)
{
#ifdef RENAME_NOREPLACE
    if (::renameat2(AT_FDCWD, temporary, AT_FDCWD, path.c_str(), RENAME_NOREPLACE) == 0) return;
    if (errno == EEXIST) fail("Save file");
    if (errno != ENOSYS && errno != EINVAL && errno != ENOTSUP && errno != EOPNOTSUPP) fail("Save file");
#endif
    // link() refuses an existing target, including a dangling symlink.
    if (::link(temporary, path.c_str()) != 0) fail("Save file");
    ::unlink(temporary);
}

}  // namespace

void writeFileAtomically(const std::filesystem::path& path, const std::string_view data, const bool overwrite)
{
    auto pattern = path.string() + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    int fd = ::mkstemp(name.data());  // created for its owner alone, and never an existing file
    if (fd < 0) fail("Create file");
    try {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto count = ::write(fd, data.data() + offset, data.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("Write file");
            offset += static_cast<std::size_t>(count);
        }
        if (::fsync(fd) != 0) fail("Flush file");
        const int closeResult = ::close(fd);
        fd = -1;
        if (closeResult != 0) fail("Close file");
        if (overwrite) {
            if (::rename(name.data(), path.c_str()) != 0) fail("Replace file");
        } else {
            putInPlaceWithoutReplacing(name.data(), path);
        }
    } catch (...) {
        if (fd >= 0) ::close(fd);
        ::unlink(name.data());
        throw;
    }
}

}  // namespace hhkbs::keymap
