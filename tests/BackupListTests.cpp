#include "app/BackupList.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ProfileFiles.h"

#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

using hhkbs::app::BackupList;
using hhkbs::keymap::Keymap;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void writeBackup(const std::filesystem::path& directory, const std::time_t when, const std::uint16_t profile)
{
    std::ofstream(directory / hhkbs::keymap::backupFileName(when, profile)) << "x";
}

void savedBackupsAreListedAndTagged(const std::filesystem::path& directory)
{
    BackupList list(directory / "saved");
    Keymap work(hhkbs::keymap::KeyboardLayout::demoProfile());
    work.setScanCode(0, 3, 0x0004);

    const auto saved = list.save(work, 2, "  before the macro ");
    require(saved.tagError.empty(), "a good tag was refused");
    require(hhkbs::keymap::readProfile(saved.path).toBytes() == work.toBytes(), "the backup does not hold the work");

    list.reload();
    require(list.entries.size() == 1 && list.entries[0].profile == 2, "the saved backup was not listed");
    require(list.entries[0].tag == "before the macro", "the tag was not saved with the backup");

    // A refused tag does not lose the backup.
    const auto other = list.save(work, 0, std::string(200, 'x'));
    require(!other.tagError.empty(), "an over-long tag was accepted");
    require(std::filesystem::exists(other.path), "the backup was lost with the refused tag");
}

void theChosenBackupCanBeTaggedAndDeleted(const std::filesystem::path& directory)
{
    const auto folder = directory / "manage";
    std::filesystem::create_directories(folder);
    writeBackup(folder, 1000000000, 0);
    writeBackup(folder, 1000000100, 1);

    BackupList list(folder);
    list.reload();
    require(list.entries.size() == 2 && !list.choice, "nothing should be chosen after a reload");

    list.choice = 1;
    const auto chosenPath = list.entries[1].path;
    list.followChosenTag();
    require(list.tagInput[0] == '\0' && list.tagShownFor == list.choice, "the tag box should start empty");

    std::snprintf(list.tagInput.data(), list.tagInput.size(), "%s", "keep");
    list.saveChosenTag();
    require(list.choice && list.entries[*list.choice].path == chosenPath, "the same backup should stay chosen");
    require(list.entries[*list.choice].tag == "keep", "the tag was not saved on the chosen backup");

    list.followChosenTag();
    require(std::string(list.tagInput.data()) == "keep", "the tag box should follow the chosen backup");
    list.choice.reset();
    list.followChosenTag();
    require(list.tagInput[0] == '\0', "the tag box should empty when nothing is chosen");

    list.choice = 0;
    const auto first = list.entries[0].path;
    list.deleteChosen();
    require(list.entries.size() == 1 && !list.choice && !std::filesystem::exists(first), "the chosen backup was not deleted");
}

void cleanUpKeepsTheNewestAndTheTagged(const std::filesystem::path& directory)
{
    const auto folder = directory / "clean";
    std::filesystem::create_directories(folder);
    for (int i = 0; i < 5; ++i) writeBackup(folder, 1000000000 + i * 100, 0);

    BackupList list(folder);
    list.reload();
    list.choice = 4;  // the oldest
    std::snprintf(list.tagInput.data(), list.tagInput.size(), "%s", "mine");
    list.saveChosenTag();

    list.keep = 2;
    require(list.surplus().size() == 2, "two untagged backups should be past the newest two");
    const auto cleaned = list.deleteSurplus();
    require(cleaned.deleted == 2 && cleaned.firstError.empty(), "the surplus was not deleted");
    require(list.entries.size() == 3, "the newest two and the tagged one should remain");
    require(list.entries.back().tag == "mine", "the tagged backup was deleted");
}

}  // namespace

int main()
{
    char name[] = "/tmp/hhkbs-backuplist-XXXXXX";
    const char* created = ::mkdtemp(name);
    if (!created) return 1;
    const std::filesystem::path directory(created);
    int result = 0;
    try {
        savedBackupsAreListedAndTagged(directory);
        theChosenBackupCanBeTaggedAndDeleted(directory);
        cleanUpKeepsTheNewestAndTheTagged(directory);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return result;
}
