#include "app/BackupList.h"
#include "keymap/ProfileFiles.h"
#include <cstdio>
#include <ctime>
#include <exception>

namespace hhkbs::app {

void BackupList::reload()
{
    entries = keymap::listBackups(directory_);
    choice.reset();
    tagShownFor.reset();
}

void BackupList::followChosenTag()
{
    if (!choice) {
        tagInput[0] = '\0';
        tagShownFor.reset();
    } else if (tagShownFor != choice) {
        std::snprintf(tagInput.data(), tagInput.size(), "%s", entries[*choice].tag.c_str());
        tagShownFor = choice;
    }
}

void BackupList::saveChosenTag()
{
    if (!choice) return;
    const auto path = entries[*choice].path;
    keymap::setBackupTag(directory_, entries[*choice], tagInput.data());
    reload();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].path == path) choice = i;
}

void BackupList::deleteChosen()
{
    if (!choice) return;
    keymap::deleteBackup(directory_, entries[*choice]);
    reload();
}

std::vector<keymap::BackupEntry> BackupList::surplus() const
{
    return keymap::backupsBeyondNewest(entries, static_cast<std::size_t>(keep));
}

BackupList::CleanUp BackupList::deleteSurplus()
{
    CleanUp result;
    for (const auto& entry : surplus()) {
        try {
            keymap::deleteBackup(directory_, entry);
            ++result.deleted;
        } catch (const std::exception& error) {
            if (result.firstError.empty()) result.firstError = error.what();
        }
    }
    reload();
    return result;
}

BackupList::Saved BackupList::save(const keymap::Keymap& work, const std::uint16_t profile, const std::string& tag) const
{
    std::filesystem::create_directories(directory_);
    Saved saved{keymap::newBackupPath(directory_, std::time(nullptr), profile), {}};
    keymap::writeProfile(saved.path, work, false);
    if (!tag.empty()) {
        try {
            for (const auto& entry : keymap::listBackups(directory_))
                if (entry.path == saved.path) keymap::setBackupTag(directory_, entry, tag);
        } catch (const std::exception& error) { saved.tagError = error.what(); }
    }
    return saved;
}

}  // namespace hhkbs::app
