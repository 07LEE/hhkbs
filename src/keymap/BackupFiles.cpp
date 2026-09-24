#include "keymap/BackupFiles.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace hhkbs::keymap {

std::filesystem::path backupDirectory()
{
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
        return std::filesystem::path(state) / "hhkbs" / "backups";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local" / "state" / "hhkbs" / "backups";
    return std::filesystem::temp_directory_path() / "hhkbs-backups";
}

std::string backupFileName(const std::time_t when, const std::uint16_t profile)
{
    char stamp[32]{};
    std::tm local{};
    localtime_r(&when, &local);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    return "backup-" + std::string(stamp) + "-profile" + std::to_string(profile + 1) + ".toml";
}

namespace {

std::optional<BackupEntry> parse(const std::filesystem::path& path)
{
    const auto name = path.filename().string();
    unsigned date = 0, time = 0, number = 0;
    int consumed = 0;
    // Fixed widths keep the name exactly as backupFileName writes it.
    if (std::sscanf(name.c_str(), "backup-%8u-%6u-profile%1u.toml%n", &date, &time, &number, &consumed) != 3
        || static_cast<std::size_t>(consumed) != name.size() || number < 1 || number > 4
        || name.size() != std::string("backup-00000000-000000-profile1.toml").size())
        return std::nullopt;

    char text[32]{};
    std::snprintf(text, sizeof text, "%04u-%02u-%02u %02u:%02u:%02u", date / 10000, date / 100 % 100,
                  date % 100, time / 10000, time / 100 % 100, time % 100);
    return BackupEntry{path, static_cast<std::uint16_t>(number - 1), text};
}

}  // namespace

std::vector<BackupEntry> listBackups(const std::filesystem::path& directory)
{
    std::vector<BackupEntry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error), end;
    while (!error && it != end) {
        std::error_code status;
        if (it->is_regular_file(status) && !status)
            if (auto entry = parse(it->path())) entries.push_back(std::move(*entry));
        it.increment(error);
    }
    std::sort(entries.begin(), entries.end(), [](const BackupEntry& a, const BackupEntry& b) {
        return a.path.filename() > b.path.filename();
    });
    return entries;
}

std::vector<BackupEntry> backupsBeyondNewest(const std::vector<BackupEntry>& newestFirst,
                                             const std::size_t keepPerProfile)
{
    std::map<std::uint16_t, std::size_t> seen;
    std::vector<BackupEntry> surplus;
    for (const auto& entry : newestFirst)
        if (seen[entry.profile]++ >= keepPerProfile) surplus.push_back(entry);
    return surplus;
}

void deleteBackup(const std::filesystem::path& directory, const BackupEntry& entry)
{
    if (entry.path.parent_path() != directory || !parse(entry.path))
        throw std::invalid_argument("Not a backup file: " + entry.path.string());
    std::error_code error;
    if (!std::filesystem::remove(entry.path, error) && !error)
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error) throw std::system_error(error, "Delete backup");
}

}  // namespace hhkbs::keymap
