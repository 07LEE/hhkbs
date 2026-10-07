#pragma once
#include "keymap/BackupFiles.h"
#include "keymap/Keymap.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hhkbs::app {

// The backups in one folder as the Backups window works with them: the list, the one chosen, the tag being typed
// for it, and the changes made to them on disk.
class BackupList {
public:
    explicit BackupList(std::filesystem::path directory) : directory_(std::move(directory)) {}

    std::vector<keymap::BackupEntry> entries;  // newest first
    std::optional<std::size_t> choice;         // the index of the chosen backup in `entries`
    std::array<char, 256> tagInput{};          // the tag being edited for the chosen backup
    std::optional<std::size_t> tagShownFor;    // the backup tagInput was filled from
    int keep = 5;                              // how many of the newest backups a clean-up keeps

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    // Reads the folder again. Nothing is chosen afterwards.
    void reload();
    // Makes the tag box follow the chosen backup: it starts from that backup's tag, and is empty when none is chosen.
    void followChosenTag();
    // Saves the typed tag on the chosen backup (blank removes it), then reloads and keeps the same one chosen.
    // Throws when the tag is refused.
    void saveChosenTag();
    // Deletes the chosen backup and reloads.
    void deleteChosen();

    // The backups a clean-up would delete.
    [[nodiscard]] std::vector<keymap::BackupEntry> surplus() const;
    struct CleanUp {
        std::size_t deleted = 0;
        std::string firstError;  // empty when every deletion worked
    };
    // Deletes the surplus, then reloads.
    CleanUp deleteSurplus();

    struct Saved {
        std::filesystem::path path;
        std::string tagError;  // empty when there was no tag or it was set
    };
    // Writes `work`, the content of `profile` (0-3), as a new backup. The tag is optional; if it is refused the
    // backup is still saved and the reason is returned. Throws when the backup cannot be written.
    [[nodiscard]] Saved save(const keymap::Keymap& work, std::uint16_t profile, const std::string& tag) const;

private:
    std::filesystem::path directory_;
};

}  // namespace hhkbs::app
