#include "keymap/BackupFiles.h"
#include "keymap/ProfileFiles.h"
#include "keymap/KeyboardLayout.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ctime>
#include <stdexcept>
#include <unistd.h>

namespace {
std::filesystem::path tagPathFor(std::filesystem::path backup)
{
    backup += ".tag";
    return backup;
}
}  // namespace

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
        touch("backup-20271231-235959-profile3.toml");
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
        // The newest come first whichever profile they belong to; the oldest are the ones that go.
        const auto surplus = backupsBeyondNewest(ordered, 2);
        if (surplus.size() != 4 || surplus[0].timestamp != "4" || surplus[3].timestamp != "2")
            throw std::runtime_error("Cleanup should drop the oldest backups, counting every profile together");
        if (backupsBeyondNewest(ordered, 5).size() != 1) throw std::runtime_error("Only the one past the newest five should go");
        if (!backupsBeyondNewest(ordered, 6).empty()) throw std::runtime_error("Nothing should go when everything fits");
        if (backupsBeyondNewest(ordered, 0).size() != ordered.size()) throw std::runtime_error("Keeping none drops all");
        // A tagged backup was kept on purpose: cleanup never drops it, and it does not use up a place.
        auto keeper = entry(0, "4");
        keeper.tag = "before the macro";
        const std::vector<BackupEntry> withTag{entry(0, "5"), keeper, entry(0, "3"), entry(0, "2")};
        const auto untaggedSurplus = backupsBeyondNewest(withTag, 1);
        if (untaggedSurplus.size() != 2 || untaggedSurplus[0].timestamp != "3" || untaggedSurplus[1].timestamp != "2")
            throw std::runtime_error("Cleanup should skip a tagged backup and not count it");
        if (backupsBeyondNewest(withTag, 0).size() != 3) throw std::runtime_error("Keeping none must still spare the tagged one");
        if (!listBackups(directory / "missing").empty()) throw std::runtime_error("Missing folder should list nothing");
        if (backupFileName(0, 0).find("-profile1.toml") == std::string::npos)
            throw std::runtime_error("Backup file names use the 1-based profile number");
        // A name written by backupFileName must be read back, whatever the clock says.
        const auto roundTrip = directory / "roundtrip";
        std::filesystem::create_directories(roundTrip);
        std::ofstream(roundTrip / backupFileName(std::time(nullptr), 2)) << "x";
        const auto written = listBackups(roundTrip);
        if (written.size() != 1 || written[0].profile != 2)
            throw std::runtime_error("A backup file name could not be read back");

        // Notes on backups.
        {
            auto backup = written[0];
            if (!backup.tag.empty()) throw std::runtime_error("A backup starts without a tag");
            setBackupTag(roundTrip, backup, "  before the macro  ");
            auto tagged = listBackups(roundTrip);
            if (tagged.size() != 1 || tagged[0].tag != "before the macro")
                throw std::runtime_error("The tag was not saved and trimmed");
            if (!std::filesystem::exists(tagPathFor(backup.path)))
                throw std::runtime_error("The tag belongs in a file next to the backup");
            for (const auto& item : std::filesystem::directory_iterator(roundTrip))
                if (item.path().string().find(".tmp") != std::string::npos) throw std::runtime_error("Temporary tag file leaked");
            const auto rejects = [&](const std::string& text) {
                try { setBackupTag(roundTrip, backup, text); } catch (const std::exception&) { return true; }
                return false;
            };
            std::string korean;
            for (std::size_t i = 0; i < maxBackupTagLength; ++i) korean += "\xEA\xB0\x80";  // 가
            if (rejects(korean)) throw std::runtime_error("A tag of the longest length should be accepted");
            if (!rejects(korean + "\xEA\xB0\x80")) throw std::runtime_error("A tag that is too long should be refused");
            if (!rejects("two\nlines")) throw std::runtime_error("A tag with a line break should be refused");
            if (listBackups(roundTrip)[0].tag != korean) throw std::runtime_error("A refused tag changed the saved one");
            const auto stranger = BackupEntry{roundTrip / "notes.toml", 0, {}};
            std::ofstream(stranger.path) << "x";
            try { setBackupTag(roundTrip, stranger, "x"); throw std::runtime_error("Tagged a file that is not a backup"); }
            catch (const std::invalid_argument&) {}
            setBackupTag(roundTrip, backup, "   ");
            if (!listBackups(roundTrip)[0].tag.empty() || std::filesystem::exists(tagPathFor(backup.path)))
                throw std::runtime_error("A blank tag should remove the tag");
            setBackupTag(roundTrip, backup, "keep");
            deleteBackup(roundTrip, backup);
            if (std::filesystem::exists(tagPathFor(backup.path))) throw std::runtime_error("Deleting a backup should delete its tag");
        }

        std::filesystem::remove_all(directory);
        std::cout << "Profile file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
