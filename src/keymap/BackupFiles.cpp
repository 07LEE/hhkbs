#include "keymap/BackupFiles.h"
#include "keymap/AtomicFile.h"
#include <algorithm>
#include <chrono>
#include <pwd.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace hhkbs::keymap {

namespace {

// The value of an environment variable when it is a path that starts at the root, which the XDG specification requires.
std::optional<std::filesystem::path> absoluteFromEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || !*value) return std::nullopt;
    const std::filesystem::path path(value);
    if (!path.is_absolute()) return std::nullopt;
    return path;
}

}  // namespace

std::filesystem::path backupDirectory()
{
    if (const auto state = absoluteFromEnvironment("XDG_STATE_HOME")) return *state / "hhkbs" / "backups";
    if (const auto home = absoluteFromEnvironment("HOME")) return *home / ".local" / "state" / "hhkbs" / "backups";
    if (const auto* account = ::getpwuid(::getuid()); account && account->pw_dir && account->pw_dir[0] == '/')
        return std::filesystem::path(account->pw_dir) / ".local" / "state" / "hhkbs" / "backups";
    // No home to be found: a folder of this user's own, not one anybody could have made first.
    return std::filesystem::path("/tmp") / ("hhkbs-" + std::to_string(::getuid())) / "backups";
}

void ensureBackupDirectory(const std::filesystem::path& directory)
{
    std::error_code error;
    if (std::filesystem::is_directory(directory, error)) return;
    // The folders that are missing, outermost first; each is made for its owner alone.
    std::vector<std::filesystem::path> missing;
    for (auto level = directory; !level.empty() && !std::filesystem::exists(level, error); level = level.parent_path()) {
        missing.push_back(level);
        if (level == level.parent_path()) break;
    }
    for (auto level = missing.rbegin(); level != missing.rend(); ++level) {
        std::filesystem::create_directory(*level);
        std::filesystem::permissions(*level, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    }
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

// "YYYYmmdd-HHMMSS" of a time, as it is written in a backup's name.
std::string stampOf(const std::time_t when) { return backupFileName(when, 0).substr(7, 15); }

// The time a stamp stands for, in local time.
std::optional<std::time_t> timeOfStamp(const std::string& stamp)
{
    std::tm local{};
    if (std::sscanf(stamp.c_str(), "%4d%2d%2d-%2d%2d%2d", &local.tm_year, &local.tm_mon, &local.tm_mday, &local.tm_hour,
                    &local.tm_min, &local.tm_sec) != 6)
        return std::nullopt;
    local.tm_year -= 1900;
    local.tm_mon -= 1;
    local.tm_isdst = -1;
    const auto time = std::mktime(&local);
    if (time == static_cast<std::time_t>(-1)) return std::nullopt;
    return time;
}

}  // namespace

std::filesystem::path newBackupPath(const std::filesystem::path& directory, const std::time_t when,
                                    const std::uint16_t profile)
{
    // A name not later than the newest backup's would list as older than it.
    std::time_t start = when;
    if (const auto existing = listBackups(directory); !existing.empty()) {
        const auto newest = existing.front().path.filename().string().substr(7, 15);
        if (stampOf(when) <= newest)
            if (const auto after = timeOfStamp(newest)) start = *after + 1;
    }
    constexpr int attempts = 600;
    for (int second = 0; second < attempts; ++second) {
        const auto path = directory / backupFileName(start + second, profile);
        std::error_code error;
        // symlink_status does not follow links, so a dangling symlink counts as taken.
        const auto status = std::filesystem::symlink_status(path, error);
        if (status.type() == std::filesystem::file_type::not_found) return path;
        // Anything else that went wrong is not a name that is taken; it will not get better by trying the next one.
        if (error) throw std::system_error(error, "Look for a free backup name");
    }
    throw std::runtime_error("No free backup file name was found");
}

namespace {

std::filesystem::path tagPath(std::filesystem::path backup)
{
    backup += ".tag";
    return backup;
}

std::string readTag(const std::filesystem::path& backup)
{
    std::ifstream file(tagPath(backup));
    std::string tag;
    std::getline(file, tag);
    while (!tag.empty() && (tag.back() == '\r' || tag.back() == ' ')) tag.pop_back();
    return tag;
}

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
            if (auto entry = parse(it->path())) {
                entry->tag = readTag(entry->path);
                entries.push_back(std::move(*entry));
            }
        it.increment(error);
    }
    std::sort(entries.begin(), entries.end(), [](const BackupEntry& a, const BackupEntry& b) {
        return a.path.filename() > b.path.filename();
    });
    return entries;
}

std::vector<BackupEntry> backupsBeyondNewest(const std::vector<BackupEntry>& newestFirst, const std::size_t keep)
{
    std::size_t seen = 0;
    std::vector<BackupEntry> surplus;
    for (const auto& entry : newestFirst) {
        if (!entry.tag.empty()) continue;
        if (seen++ >= keep) surplus.push_back(entry);
    }
    return surplus;
}

void setBackupTag(const std::filesystem::path& directory, const BackupEntry& entry, const std::string& tag)
{
    std::error_code status;
    if (entry.path.parent_path() != directory || !parse(entry.path) || !std::filesystem::is_regular_file(entry.path, status))
        throw std::invalid_argument("Not a backup file: " + entry.path.string());

    const auto first = tag.find_first_not_of(' ');
    const auto text = first == std::string::npos ? std::string() : tag.substr(first, tag.find_last_not_of(' ') - first + 1);
    std::size_t characters = 0;
    for (const unsigned char byte : text) {
        if (byte < 0x20 || byte == 0x7F) throw std::invalid_argument("A tag cannot contain control characters");
        if ((byte & 0xC0) != 0x80) ++characters;  // count UTF-8 characters, not bytes
    }
    if (characters > maxBackupTagLength)
        throw std::invalid_argument("A tag can have at most " + std::to_string(maxBackupTagLength) + " characters");

    const auto target = tagPath(entry.path);
    std::error_code error;
    if (text.empty()) {
        std::filesystem::remove(target, error);
        if (error) throw std::system_error(error, "Remove tag");
        return;
    }
    // Written aside, flushed and renamed, so a failed or interrupted write leaves the tag that was there as it was.
    writeFileAtomically(target, text + '\n', true);
}

void deleteBackup(const std::filesystem::path& directory, const BackupEntry& entry)
{
    if (entry.path.parent_path() != directory || !parse(entry.path))
        throw std::invalid_argument("Not a backup file: " + entry.path.string());
    std::error_code error;
    // A folder with a backup's name is not a backup, and remove() would take it away with an empty one.
    if (std::filesystem::symlink_status(entry.path, error).type() == std::filesystem::file_type::directory)
        throw std::invalid_argument("Not a backup file: " + entry.path.string());
    if (!std::filesystem::remove(entry.path, error) && !error)
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error) throw std::system_error(error, "Delete backup");
    std::filesystem::remove(tagPath(entry.path), error);  // a missing note is fine
}

std::size_t removeStrayFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> stray;
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::directory_iterator it(directory, error), end;
    while (!error && it != end) {
        std::error_code status;
        if (it->is_regular_file(status) && !status) {
            const auto name = it->path().filename().string();
            if (name.ends_with(".tag") && parse(it->path().parent_path() / name.substr(0, name.size() - 4))) {
                std::error_code exists;
                // A note whose backup has gone.
                if (!std::filesystem::exists(it->path().parent_path() / name.substr(0, name.size() - 4), exists) && !exists)
                    stray.push_back(it->path());
            } else if (const auto mark = name.find(".toml"); mark != std::string::npos && name.find(".tmp.", mark) != std::string::npos &&
                       parse(it->path().parent_path() / name.substr(0, mark + 5))) {
                // What a write that was cut short left; one that is still being written is too young to be taken.
                const auto written = it->last_write_time(status);
                if (!status && now - written > std::chrono::hours(1)) stray.push_back(it->path());
            }
        }
        it.increment(error);
    }
    std::size_t removed = 0;
    for (const auto& path : stray) {
        std::error_code ignored;
        if (std::filesystem::remove(path, ignored)) ++removed;
    }
    return removed;
}

}  // namespace hhkbs::keymap
