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

// $XDG_STATE_HOME/hhkbs/backups, or ~/.local/state/hhkbs/backups. A relative XDG_STATE_HOME or HOME is not used, as the
// XDG specification says; the account's home folder is the last resort, and /tmp/hhkbs-<user id> when there is none.
[[nodiscard]] std::filesystem::path backupDirectory();

// Makes sure the folder exists. Folders made here can be entered by their owner only, since a backup holds the
// keyboard's whole configuration. Throws std::filesystem::filesystem_error.
void ensureBackupDirectory(const std::filesystem::path& directory);

// backup-YYYYmmdd-HHMMSS-profileN.toml, where N is the 1-based profile number. The time is local time.
[[nodiscard]] std::string backupFileName(std::time_t when, std::uint16_t profile);

// A path in `directory` for a new backup of `profile` that nothing uses yet. A name sorts as newer than every backup
// already there, whichever profile it is for: when the name for `when` is taken, or is not later than the newest one
// (the clock was set back, or the local time repeated at the end of summer time), the time is moved on a second at a
// time, so the name keeps its form and the newer backup still lists as newer. Throws when no free name is found or
// the folder cannot be looked at.
[[nodiscard]] std::filesystem::path newBackupPath(const std::filesystem::path& directory, std::time_t when,
                                                  std::uint16_t profile);

// Backups in the directory, newest first. Files with any other name are ignored.
[[nodiscard]] std::vector<BackupEntry> listBackups(const std::filesystem::path& directory);

// Backups that fall outside the newest `keep`, whichever profile they came from. Expects newest first. A backup
// with a tag was kept on purpose: it is never returned, and does not count towards the ones to keep.
[[nodiscard]] std::vector<BackupEntry> backupsBeyondNewest(const std::vector<BackupEntry>& newestFirst,
                                                            std::size_t keep);

// Attaches a one-line note to a backup, or removes it when `tag` is blank. The note is kept in a small file next to
// the backup (<backup>.tag), so the backup itself is never rewritten. Throws for a tag that is too long or has
// control characters, and for anything that is not an existing backup directly inside `directory`.
void setBackupTag(const std::filesystem::path& directory, const BackupEntry& entry, const std::string& tag);

// Deletes one backup and its note. Refuses anything that is not a backup file directly inside `directory`, a folder
// that has a backup's name included.
void deleteBackup(const std::filesystem::path& directory, const BackupEntry& entry);

// Removes what is left behind in `directory`: notes whose backup is gone, and temporary files of a write that was cut
// short (older than an hour, so one being written now is left alone). Only files with the names this program gives
// them are touched. Returns how many were removed.
std::size_t removeStrayFiles(const std::filesystem::path& directory);

}  // namespace hhkbs::keymap
