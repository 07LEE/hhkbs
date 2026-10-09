#include "keymap/BackupFiles.h"
#include "keymap/ProfileFiles.h"
#include "keymap/KeyboardLayout.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ctime>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
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
        // Names that only look like a backup: another case, a wrong width, a longer profile number, a doubled or
        // missing extension, and the note kept next to a valid backup.
        touch("Backup-20260101-090000-profile1.toml");
        touch("backup-20260101-0900000-profile1.toml");
        touch("backup-202601011-090000-profile1.toml");
        touch("backup-20260101-090000-profile12.toml");
        touch("backup-20260101-090000-profile1.toml.toml");
        touch("backup-20260101-090000-profile1");
        touch("backup-20260101-090000-profile1.TOML");
        touch("backup-20260101-090000-profile1.toml.tag");
        touch("backup-20260101-090000-profile1.toml ");
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

        // Two backups of one profile in the same second get different names, and the later one lists as newer.
        {
            const auto crowded = directory / "crowded";
            std::filesystem::create_directories(crowded);
            const std::time_t now = std::time(nullptr);
            const auto first = newBackupPath(crowded, now, 0);
            std::ofstream(first) << "x";
            const auto second = newBackupPath(crowded, now, 0);
            if (second == first) throw std::runtime_error("A second backup in the same second must not reuse the name");
            std::ofstream(second) << "x";
            const auto listed = listBackups(crowded);
            if (listed.size() != 2 || listed[0].path != second || listed[1].path != first)
                throw std::runtime_error("Backups from the same second must be listed with the later one first");
            // A backup of another profile made after them lists as the newest, so it cannot take their second either.
            const auto third = newBackupPath(crowded, now, 1);
            std::ofstream(third) << "x";
            const auto afterThird = listBackups(crowded);
            if (afterThird.size() != 3 || afterThird[0].path != third || afterThird[0].profile != 1)
                throw std::runtime_error("A backup made after the others must list as the newest, whichever profile it is for");
        }

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

        // What readProfile refuses before it reads anything, and says why.
        {
            const auto refusal = [&](const std::filesystem::path& file) {
                try { static_cast<void>(readProfile(file)); } catch (const std::exception& error) { return std::string(error.what()); }
                return std::string();
            };
            const auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
            if (!has(refusal(directory / "nothing-here.toml"), "Could not open")) throw std::runtime_error("A missing file was not reported as such");
            if (!has(refusal(directory), "folder")) throw std::runtime_error("A folder was not reported as a folder");
            const auto pipe = directory / "pipe.toml";
            if (::mkfifo(pipe.c_str(), 0600) != 0) throw std::runtime_error("could not make a pipe");
            if (!has(refusal(pipe), "not a profile file")) throw std::runtime_error("A pipe was read, or not reported");
            const auto huge = directory / "huge.toml";
            { std::ofstream file(huge, std::ios::binary); file << std::string(maxProfileFileBytes + 1, '#'); }
            if (!has(refusal(huge), "too large")) throw std::runtime_error("A file far larger than any profile was read");
            { std::ofstream file(directory / "empty.toml"); }
            if (refusal(directory / "empty.toml").empty()) throw std::runtime_error("An empty file was taken for a profile");
        }

        // A folder that has a backup's name is not a backup, and is not deleted as one.
        {
            const auto lookalike = backups / "backup-20250101-000000-profile1.toml";
            std::ofstream(lookalike / "precious.txt") << "x";
            bool refused = false;
            try { deleteBackup(backups, BackupEntry{lookalike, 0, "2025-01-01 00:00:00"}); }
            catch (const std::invalid_argument&) { refused = true; }
            if (!refused) throw std::runtime_error("A folder with a backup's name was deleted");
            if (!std::filesystem::exists(lookalike / "precious.txt")) throw std::runtime_error("The folder's content was lost");
        }

        // Tags are written whole, for their owner only, and a tag that cannot be saved leaves the old one alone.
        {
            const auto folder = directory / "tags";
            std::filesystem::create_directories(folder);
            const auto name = folder / backupFileName(std::time(nullptr), 0);
            std::ofstream(name) << "x";
            auto entry = listBackups(folder).front();
            setBackupTag(folder, entry, "first");
            struct stat info{};
            if (::stat(tagPathFor(name).c_str(), &info) != 0 || (info.st_mode & 0777) != 0600)
                throw std::runtime_error("A tag file should be readable and writable by its owner only");
            if (::geteuid() != 0) {
                std::filesystem::permissions(folder, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
                bool failed = false;
                try { setBackupTag(folder, entry, "second"); } catch (const std::exception&) { failed = true; }
                std::filesystem::permissions(folder, std::filesystem::perms::owner_all);
                if (!failed) throw std::runtime_error("A tag that could not be written was reported as saved");
                if (listBackups(folder).front().tag != "first") throw std::runtime_error("A failed write changed the tag");
                for (const auto& item : std::filesystem::directory_iterator(folder))
                    if (item.path().string().find(".tmp") != std::string::npos) throw std::runtime_error("A failed write left a temporary file");
            }
        }

        // Where backups go: only absolute locations count, and what is made is for the owner alone.
        {
            const auto remember = [](const char* name) { const char* value = std::getenv(name); return value ? std::string(value) : std::string(); };
            const auto state = remember("XDG_STATE_HOME"), home = remember("HOME");
            ::setenv("XDG_STATE_HOME", "relative/state", 1);
            ::setenv("HOME", "/home/someone", 1);
            const auto fromHome = backupDirectory();
            ::setenv("XDG_STATE_HOME", "/var/lib/state", 1);
            const auto fromState = backupDirectory();
            ::setenv("HOME", "also/relative", 1);
            ::unsetenv("XDG_STATE_HOME");
            const auto relativeEverywhere = backupDirectory();
            if (state.empty()) ::unsetenv("XDG_STATE_HOME"); else ::setenv("XDG_STATE_HOME", state.c_str(), 1);
            if (home.empty()) ::unsetenv("HOME"); else ::setenv("HOME", home.c_str(), 1);
            if (fromHome != std::filesystem::path("/home/someone/.local/state/hhkbs/backups"))
                throw std::runtime_error("A relative XDG_STATE_HOME was used: " + fromHome.string());
            if (fromState != std::filesystem::path("/var/lib/state/hhkbs/backups")) throw std::runtime_error("An absolute XDG_STATE_HOME was not used");
            if (!relativeEverywhere.is_absolute()) throw std::runtime_error("A relative folder was used for backups");

            const auto base = directory / "newplace";
            const auto target = base / "state" / "hhkbs" / "backups";
            std::filesystem::create_directories(base);
            std::filesystem::permissions(base, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::others_read);
            ensureBackupDirectory(target);
            const auto modeOf = [](const std::filesystem::path& folder) { return std::filesystem::status(folder).permissions() & std::filesystem::perms::mask; };
            if (modeOf(target) != std::filesystem::perms::owner_all || modeOf(target.parent_path()) != std::filesystem::perms::owner_all)
                throw std::runtime_error("Folders made for backups can be entered by others");
            if (modeOf(base) == std::filesystem::perms::owner_all) throw std::runtime_error("A folder that was there before was changed");
            ensureBackupDirectory(target);  // again: nothing to do, nothing to fail
        }

        // A name is never taken when the clock repeats, or when the folder cannot be looked at.
        {
            const auto folder = directory / "clockwork";
            std::filesystem::create_directories(folder);
            const char* zone = std::getenv("TZ");
            const std::string kept = zone ? zone : "";
            ::setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);  // summer time ends on 1 November 2026, at 2:00 becoming 1:00
            ::tzset();
            std::ofstream(folder / "backup-20261101-013000-profile1.toml") << "x";  // made in the first 1:30
            std::tm second{};  // 1:10 for the second time, an hour after the clocks went back
            second.tm_year = 2026 - 1900; second.tm_mon = 10; second.tm_mday = 1; second.tm_hour = 6; second.tm_min = 10;
            const auto repeated = newBackupPath(folder, timegm(&second), 0).filename().string();
            if (zone) ::setenv("TZ", kept.c_str(), 1); else ::unsetenv("TZ");
            ::tzset();
            if (repeated.substr(7, 15) <= std::string("20261101-013000"))
                throw std::runtime_error("A backup made after another was named as if it were older: " + repeated);

            if (::geteuid() != 0) {
                const auto locked = directory / "locked";
                std::filesystem::create_directories(locked);
                std::filesystem::permissions(locked, std::filesystem::perms::none);
                const auto started = std::chrono::steady_clock::now();
                bool threw = false;
                try { static_cast<void>(newBackupPath(locked, std::time(nullptr), 0)); } catch (const std::system_error&) { threw = true; }
                std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
                if (!threw) throw std::runtime_error("A folder that cannot be looked at was given a name");
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(2)) throw std::runtime_error("It tried hundreds of names first");
            }
        }

        // What is left behind is cleaned up, and nothing else is touched.
        {
            const auto folder = directory / "strays";
            std::filesystem::create_directories(folder);
            const auto make = [&](const std::string& fileName, const bool old) {
                std::ofstream(folder / fileName) << "x";
                if (old) std::filesystem::last_write_time(folder / fileName, std::filesystem::file_time_type::clock::now() - std::chrono::hours(3));
            };
            make("backup-20260101-090000-profile1.toml", false);
            make("backup-20260101-090000-profile1.toml.tag", false);         // its note: kept
            make("backup-20260102-090000-profile1.toml.tag", false);         // a note whose backup is gone
            make("backup-20260101-090000-profile1.toml.tmp.aB3dEf", true);   // a write cut short long ago
            make("backup-20260101-090000-profile1.toml.tag.tmp.aB3dEf", true);
            make("backup-20260101-090000-profile1.toml.tmp.zZ9yXw", false);  // one being written just now
            make("notes.tag", false);                                       // not ours
            make("holiday.tmp.abcdef", true);                               // not ours
            if (removeStrayFiles(folder) != 3) throw std::runtime_error("The wrong number of stray files was removed");
            const auto exists = [&](const char* fileName) { return std::filesystem::exists(folder / fileName); };
            if (!exists("backup-20260101-090000-profile1.toml") || !exists("backup-20260101-090000-profile1.toml.tag")
                || !exists("backup-20260101-090000-profile1.toml.tmp.zZ9yXw") || !exists("notes.tag") || !exists("holiday.tmp.abcdef"))
                throw std::runtime_error("A file that was not stray was removed");
            if (exists("backup-20260102-090000-profile1.toml.tag") || exists("backup-20260101-090000-profile1.toml.tmp.aB3dEf"))
                throw std::runtime_error("A stray file was left");
            if (removeStrayFiles(directory / "no-such-folder") != 0) throw std::runtime_error("A missing folder has nothing to clean");
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
