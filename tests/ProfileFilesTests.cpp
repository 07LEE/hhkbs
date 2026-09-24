#include "keymap/BackupFiles.h"
#include "keymap/ProfileFiles.h"
#include "keymap/KeyboardLayout.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ctime>
#include <stdexcept>
#include <unistd.h>

int main() {
    char name[] = "/tmp/hhkbs-files-XXXXXX";
    const char* created = ::mkdtemp(name);
    if (!created) return 1;
    const std::filesystem::path directory(created);
    try {
        using namespace hhkbs::keymap;
        Keymap original(KeyboardLayout::demoProfile());
        const auto path = directory / "profile.toml";
        writeProfile(path, original, false);
        auto edited = original;
        edited.setScanCode(3, 117, 0xFFFF);
        bool refused = false;
        try { writeProfile(path, edited, false); } catch (const std::exception&) { refused = true; }
        if (!refused || readProfile(path).toBytes() != original.toBytes())
            throw std::runtime_error("Unconfirmed export replaced an existing profile");
        writeProfile(path, edited, true);
        if (readProfile(path).toBytes() != edited.toBytes()) throw std::runtime_error("Round trip failed");
        const auto link = directory / "link.toml";
        std::filesystem::create_symlink(directory/"missing.toml", link);
        refused = false;
        try { writeProfile(link, edited, false); } catch (const std::exception&) { refused = true; }
        if (!refused || !std::filesystem::is_symlink(link)) throw std::runtime_error("Symlink was overwritten");
        const auto invalid = directory / "invalid.toml";
        { std::ofstream file(invalid); file << "invalid profile"; }
        refused = false;
        try { static_cast<void>(readProfile(invalid)); } catch (const std::exception&) { refused = true; }
        if (!refused) throw std::runtime_error("Invalid profile accepted");
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.path().string().find(".tmp.") != std::string::npos)
                throw std::runtime_error("Temporary file leaked");

        const auto backups = directory / "backups";
        std::filesystem::create_directories(backups);
        const auto touch = [&](const std::string& fileName) { std::ofstream(backups / fileName) << "x"; };
        touch("backup-20260101-090000-profile1.toml");
        touch("backup-20260924-094806-profile4.toml");
        touch(backupFileName(std::time(nullptr), 2));
        touch("backup-20260101-090000-profile5.toml");
        touch("backup-20260101-090000-profile0.toml");
        touch("backup-2026-090000-profile1.toml");
        touch("backup-20260101-090000-profile1.toml.bak");
        touch("notes.toml");
        std::filesystem::create_directories(backups / "backup-20250101-000000-profile1.toml");
        const auto listed = listBackups(backups);
        if (listed.size() != 3) throw std::runtime_error("Backup list should hold exactly the three valid files");
        if (listed[0].profile != 2 || listed[1].profile != 3 || listed[2].profile != 0)
            throw std::runtime_error("Backups were not listed newest first with their profile");
        if (listed[1].timestamp != "2026-09-24 09:48:06" || listed[2].timestamp != "2026-01-01 09:00:00")
            throw std::runtime_error("Backup timestamp was not formatted");
        const auto refuses = [&](const BackupEntry& entry) {
            try { deleteBackup(backups, entry); } catch (const std::exception&) { return true; }
            return false;
        };
        if (!refuses({backups / "notes.toml", 0, {}})) throw std::runtime_error("A non-backup file was deleted");
        if (!refuses({directory / "profile.toml", 0, {}})) throw std::runtime_error("A file outside the backups was deleted");
        if (!refuses({backups / "backup-20991231-235959-profile1.toml", 0, {}}))
            throw std::runtime_error("Deleting a missing backup should fail");
        if (!std::filesystem::exists(backups / "notes.toml") || !std::filesystem::exists(directory / "profile.toml"))
            throw std::runtime_error("A refused delete still removed a file");
        deleteBackup(backups, listed[1]);
        const auto remaining = listBackups(backups);
        if (remaining.size() != 2 || std::filesystem::exists(listed[1].path))
            throw std::runtime_error("The chosen backup was not deleted alone");
        const auto entry = [](std::uint16_t profile, const char* stamp) {
            return BackupEntry{"backup-" + std::string(stamp) + ".toml", profile, stamp};
        };
        const std::vector<BackupEntry> ordered{entry(0, "5"), entry(3, "5"), entry(0, "4"), entry(0, "3"), entry(3, "3"), entry(0, "2")};
        const auto surplus = backupsBeyondNewest(ordered, 2);
        if (surplus.size() != 2 || surplus[0].timestamp != "3" || surplus[0].profile != 0 || surplus[1].timestamp != "2")
            throw std::runtime_error("Cleanup should drop only the oldest backups of each profile");
        if (!backupsBeyondNewest(ordered, 4).empty()) throw std::runtime_error("Nothing should go when everything fits");
        if (backupsBeyondNewest(ordered, 0).size() != ordered.size()) throw std::runtime_error("Keeping none drops all");
        if (!listBackups(directory / "missing").empty()) throw std::runtime_error("Missing folder should list nothing");
        if (backupFileName(0, 0).find("-profile1.toml") == std::string::npos)
            throw std::runtime_error("Backup file names use the 1-based profile number");

        std::filesystem::remove_all(directory);
        std::cout << "Profile file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
