#pragma once
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace hhkbs::keymap {

struct BackupEntry {
    std::filesystem::path path;
    std::uint16_t profile{};  // 0-3
    std::string timestamp;    // YYYY-MM-DD HH:MM:SS, local time
    std::string tag{};        // a note the user attached; empty when there is none
};

inline constexpr std::size_t maxBackupTagLength = 40;  // characters

// $XDG_STATE_HOME/hhkbs/backups, or ~/.local/state/hhkbs/backups.
[[nodiscard]] std::filesystem::path backupDirectory();

// backup-YYYYmmdd-HHMMSS-profileN.toml, where N is the 1-based profile number.
[[nodiscard]] std::string backupFileName(std::time_t when, std::uint16_t profile);

// Backups in the directory, newest first. Files with any other name are ignored.
[[nodiscard]] std::vector<BackupEntry> listBackups(const std::filesystem::path& directory);

// Backups that fall outside the newest `keepPerProfile` of their own profile. Expects newest first. A backup with a
// tag was kept on purpose: it is never returned, and does not count towards the ones to keep.
[[nodiscard]] std::vector<BackupEntry> backupsBeyondNewest(const std::vector<BackupEntry>& newestFirst,
                                                            std::size_t keepPerProfile);

// Attaches a one-line note to a backup, or removes it when `tag` is blank. The note is kept in a small file next to
// the backup (<backup>.tag), so the backup itself is never rewritten. Throws for a tag that is too long or has
// control characters, and for anything that is not an existing backup directly inside `directory`.
void setBackupTag(const std::filesystem::path& directory, const BackupEntry& entry, const std::string& tag);

// Deletes one backup and its note. Refuses anything that is not a backup file directly inside `directory`.
void deleteBackup(const std::filesystem::path& directory, const BackupEntry& entry);

}  // namespace hhkbs::keymap
