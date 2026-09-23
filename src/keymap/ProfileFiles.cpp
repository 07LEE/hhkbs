#include "keymap/ProfileFiles.h"
#include "keymap/ProfileSerializer.h"
#include <cerrno>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <unistd.h>

namespace hhkbs::keymap {
Keymap readProfile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open the profile file.");
    std::string text{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) throw std::runtime_error("Could not read the profile file.");
    return ProfileSerializer::fromToml(text);
}

void writeProfile(const std::filesystem::path& path, const Keymap& keymap, bool overwrite)
{
    const auto text = ProfileSerializer::toToml(keymap);
    auto pattern = path.string() + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    int fd = ::mkstemp(name.data());
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create export file");
    try {
        std::size_t offset = 0;
        while (offset < text.size()) {
            const auto count = ::write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::system_error(errno, std::generic_category(), "Write profile");
            offset += static_cast<std::size_t>(count);
        }
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "Flush profile");
        const int closeResult = ::close(fd);
        fd = -1;
        if (closeResult != 0) throw std::system_error(errno, std::generic_category(), "Close profile");
        if (overwrite) {
            if (::rename(name.data(), path.c_str()) != 0)
                throw std::system_error(errno, std::generic_category(), "Replace profile");
        } else {
            // link() refuses an existing target, including a dangling symlink.
            if (::link(name.data(), path.c_str()) != 0)
                throw std::system_error(errno, std::generic_category(), "Save profile");
        }
        ::unlink(name.data());
    } catch (...) {
        if (fd >= 0) ::close(fd);
        ::unlink(name.data());
        throw;
    }
}
}
