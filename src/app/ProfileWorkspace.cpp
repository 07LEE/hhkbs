#include "app/ProfileWorkspace.h"
#include <utility>

namespace hhkbs::app {

bool ProfileWorkspace::unsaved() const { return loaded && keymap.toBytes() != savedBytes; }

bool ProfileWorkspace::anyUnsaved() const
{
    if (unsaved()) return true;
    for (const auto& stash : stashed)
        if (stash && stash->keymap.toBytes() != stash->savedBytes) return true;
    return false;
}

std::string ProfileWorkspace::unsavedList() const
{
    std::string list;
    const auto add = [&](const std::string& name) { list += (list.empty() ? "" : ", ") + name; };
    if (unsaved()) add(selected ? "Profile " + std::to_string(*selected + 1) + " (on screen)" : "the profile on screen");
    for (std::uint16_t i = 0; i < 4; ++i)
        if (stashed[i] && stashed[i]->keymap.toBytes() != stashed[i]->savedBytes) add("Profile " + std::to_string(i + 1));
    return list;
}

const keymap::Keymap* ProfileWorkspace::draft(const std::uint16_t profile) const
{
    if (selected == profile) return loaded ? &keymap : nullptr;
    return stashed[profile] ? &stashed[profile]->keymap : nullptr;
}

std::vector<std::uint16_t> ProfileWorkspace::editedProfiles() const
{
    std::vector<std::uint16_t> profiles;
    for (std::uint16_t i = 0; i < 4; ++i)
        if (const auto* work = draft(i); work && work->isModified()) profiles.push_back(i);
    return profiles;
}

void ProfileWorkspace::stashShown()
{
    if (selected && loaded) stashed[*selected] = Stash{keymap, savedBytes, summary};
}

void ProfileWorkspace::showStashed(const std::uint16_t profile)
{
    auto stash = std::move(*stashed[profile]);
    stashed[profile].reset();
    keymap = std::move(stash.keymap);
    savedBytes = std::move(stash.savedBytes);
    summary = std::move(stash.summary);
    selected = profile;
    loaded = true;
}

bool ProfileWorkspace::showFirstUnsaved()
{
    if (unsaved()) return false;
    for (std::uint16_t i = 0; i < 4; ++i) {
        if (stashed[i] && stashed[i]->keymap.toBytes() != stashed[i]->savedBytes) {
            stashShown();
            showStashed(i);
            return true;
        }
    }
    return false;
}

void ProfileWorkspace::useKeyboardAsReference()
{
    if (selected && !keyboardBytes[*selected].empty()) keymap.rebase(keyboardBytes[*selected]);
}

void ProfileWorkspace::adoptKeyboardProfile(const std::uint16_t profile, const std::vector<std::uint8_t>& bytes,
                                            const std::string& serial)
{
    keyboardSerial = serial;
    keyboardBytes[profile] = bytes;
    keymap::Keymap fresh(bytes);
    if (selected != profile) {
        // Another profile comes onto the screen: the one that was there is kept, and this one continues where
        // it was left, with edits it may have. Only the keyboard's content is brought up to date.
        stashShown();
        if (stashed[profile]) showStashed(profile);
        if (selected == profile && keymap.isModified()) keymap.rebase(bytes);
        else keymap = std::move(fresh);
    } else keymap = std::move(fresh);
    if (!keymap.isModified()) savedBytes = keymap.toBytes();
    loaded = true;
    selected = profile;
}

void ProfileWorkspace::markWritten(const std::uint16_t profile, const std::vector<std::uint8_t>& bytes)
{
    keyboardBytes[profile] = bytes;
    if (selected == profile) {
        keymap.rebase(bytes);
        savedBytes = keymap.toBytes();
    } else if (stashed[profile]) {
        stashed[profile]->keymap.rebase(bytes);
        stashed[profile]->savedBytes = stashed[profile]->keymap.toBytes();
    }
}

}  // namespace hhkbs::app
