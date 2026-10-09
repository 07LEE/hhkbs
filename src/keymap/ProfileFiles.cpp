#include "keymap/ProfileFiles.h"
#include "keymap/AtomicFile.h"
#include "keymap/ProfileSerializer.h"
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace hhkbs::keymap {

Keymap readProfile(const std::filesystem::path& path)
{
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0) throw std::runtime_error("Could not open the profile file.");
    // Checked before opening: opening a pipe or a device could wait forever, and a profile is a few kilobytes.
    if (S_ISDIR(info.st_mode)) throw std::runtime_error("That is a folder, not a profile file.");
    if (!S_ISREG(info.st_mode)) throw std::runtime_error("That is not a profile file.");
    if (info.st_size > static_cast<off_t>(maxProfileFileBytes)) throw std::runtime_error("That file is too large to be a profile.");

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) throw std::runtime_error("Could not open the profile file.");
    std::string text;
    char buffer[4096];
    std::size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        text.append(buffer, count);
        if (text.size() > maxProfileFileBytes) { std::fclose(file); throw std::runtime_error("That file is too large to be a profile."); }
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) throw std::runtime_error("Could not read the profile file.");
    return ProfileSerializer::fromToml(text);
}

void writeProfile(const std::filesystem::path& path, const Keymap& keymap, const bool overwrite)
{
    writeFileAtomically(path, ProfileSerializer::toToml(keymap), overwrite);
}

}  // namespace hhkbs::keymap
