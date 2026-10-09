#pragma once
#include "keymap/Keymap.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hhkbs::app {

// The profile on screen and the work kept for the others. Each of the keyboard's four profiles keeps its own edits:
// leaving one puts it aside (stashes it), and choosing it again brings it back as it was left.
class ProfileWorkspace {
public:
    // The work on a profile that is not the one shown.
    struct Stash {
        keymap::Keymap keymap;
        std::vector<std::uint8_t> savedBytes;
        std::string summary;
    };

    keymap::Keymap keymap;                  // the profile on screen, with its edits
    std::vector<std::uint8_t> savedBytes;   // what it held when it was last saved, loaded or applied
    std::string summary;                    // where it came from, for the caption
    bool loaded = false;                    // there is something on screen
    std::optional<std::uint16_t> selected;  // the keyboard profile on screen; only set once it has been read from the keyboard
    std::array<std::optional<Stash>, 4> stashed;
    // What the keyboard held for each profile when it was last read or written; empty when it has not been read.
    std::array<std::vector<std::uint8_t>, 4> keyboardBytes;
    // The keyboard those bytes came from. Writes only go to the keyboard with this serial number.
    std::string keyboardSerial;

    // The profile on screen has changes that were not saved.
    [[nodiscard]] bool unsaved() const;
    // Any profile, on screen or put aside, has changes that were not saved.
    [[nodiscard]] bool anyUnsaved() const;
    // The profiles with unsaved work, for the prompt shown when the window is closed.
    [[nodiscard]] std::string unsavedList() const;
    // Any profile put aside has unsaved work, and which ones, as "Profile 1, Profile 3".
    [[nodiscard]] bool stashedUnsaved() const;
    [[nodiscard]] std::string stashedUnsavedList() const;
    // Reading `profile` from the keyboard would throw away what is on screen, with edits that were not saved. That is
    // the case when nothing on screen came from the keyboard (a file, a backup, the defaults), or when it is the same
    // profile. Reading another profile puts the one on screen aside, so it loses nothing.
    [[nodiscard]] bool readWouldDiscard(std::uint16_t profile) const;
    // `serial` belongs to another keyboard than the one the profiles here were read from.
    [[nodiscard]] bool keyboardChanged(const std::string& serial) const;
    // The work on a profile, wherever it is kept: the editor for the one on screen, a stash for the others.
    [[nodiscard]] const keymap::Keymap* draft(std::uint16_t profile) const;
    // The profiles whose work differs from what the keyboard holds, so applying would change something.
    [[nodiscard]] std::vector<std::uint16_t> editedProfiles() const;

    void stashShown();
    // Brings a stashed profile onto the screen. The one that was there must have been stashed first.
    void showStashed(std::uint16_t profile);
    // Brings a profile with unsaved work onto the screen when the one shown has none, so the prompt's Save acts on
    // it. Returns whether the screen changed.
    bool showFirstUnsaved();
    // Content that came from a file or a backup is compared with what the keyboard holds for the shown profile, when
    // that has been read, so the keys it would change are marked and Discard changes goes back to the keyboard's content.
    void useKeyboardAsReference();

    // Content that came from a file, a backup or the defaults comes onto the screen as it is. It is compared with what
    // the keyboard holds for the profile on screen, when that has been read, and counts as saved.
    void loadContent(keymap::Keymap content, std::string description);

    // A profile was read from the keyboard with `serial`. It comes onto the screen; edits it had are kept. When it is
    // another keyboard than before, everything read from the previous one is forgotten, since it no longer describes
    // what is connected.
    void adoptKeyboardProfile(std::uint16_t profile, const std::vector<std::uint8_t>& bytes, const std::string& serial);
    // A profile was written to the keyboard: it now holds its work, so it stops counting as changed.
    void markWritten(std::uint16_t profile, const std::vector<std::uint8_t>& bytes);
};

}  // namespace hhkbs::app
