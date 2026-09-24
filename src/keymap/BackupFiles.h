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
};

// $XDG_STATE_HOME/hhkbs/backups, or ~/.local/state/hhkbs/backups.
[[nodiscard]] std::filesystem::path backupDirectory();

// backup-YYYYmmdd-HHMMSS-profileN.toml, where N is the 1-based profile number.
[[nodiscard]] std::string backupFileName(std::time_t when, std::uint16_t profile);

// Backups in the directory, newest first. Files with any other name are ignored.
[[nodiscard]] std::vector<BackupEntry> listBackups(const std::filesystem::path& directory);

// Deletes one backup. Refuses anything that is not a backup file directly inside `directory`.
void deleteBackup(const std::filesystem::path& directory, const BackupEntry& entry);

}  // namespace hhkbs::keymap
