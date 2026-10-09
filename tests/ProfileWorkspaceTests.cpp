#include "app/ProfileWorkspace.h"
#include "keymap/KeyboardLayout.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using hhkbs::app::ProfileWorkspace;
using hhkbs::keymap::Keymap;

void require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> keyboardProfile()
{
    return hhkbs::keymap::KeyboardLayout::demoProfile();
}

void readingAProfileShowsItClean()
{
    ProfileWorkspace work;
    work.adoptKeyboardProfile(1, keyboardProfile(), "serial");

    require(work.loaded && work.selected == 1, "the profile read was not put on screen");
    require(work.keyboardSerial == "serial", "the keyboard's serial number was not kept");
    require(!work.unsaved() && !work.anyUnsaved(), "a profile just read counted as unsaved");
    require(work.editedProfiles().empty(), "a profile just read counted as edited");
    require(work.draft(1) != nullptr && work.draft(0) == nullptr, "only the profile on screen has work");
}

void editsSurviveLeavingAndReturning()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    work.keymap.setScanCode(0, 3, 0x0004);
    require(work.unsaved() && work.editedProfiles() == std::vector<std::uint16_t>{0}, "an edit was not noticed");

    // Another profile comes onto the screen; the first is put aside with its edit.
    work.adoptKeyboardProfile(2, bytes, "serial");
    require(work.selected == 2 && !work.unsaved(), "the other profile should be shown clean");
    require(work.anyUnsaved(), "the profile put aside lost its unsaved mark");
    require(work.draft(0) != nullptr && work.draft(0)->scanCode(0, 3) == 0x0004, "the edit was not kept aside");
    require(work.unsavedList() == "Profile 1", "the unsaved profile was not listed");

    // Reading the first again brings its edit back and only updates what the keyboard holds.
    auto changed = bytes;
    changed[0] ^= 0x01;
    work.adoptKeyboardProfile(0, changed, "serial");
    require(work.selected == 0 && work.keymap.scanCode(0, 3) == 0x0004, "the edit was lost on returning");
    require(work.keyboardBytes[0] == changed, "the keyboard's content was not brought up to date");
    require(work.unsavedList() == "Profile 1 (on screen)", "the profile on screen was not listed");
    // The keys stay as the user had them; only what they are compared with is the keyboard's new content. So the key
    // the keyboard changed meanwhile now shows as different from the keyboard, next to the user's own edit.
    require(work.keymap.scanCode(0, 0) == Keymap(bytes).scanCode(0, 0), "a key the user did not touch was changed");
    require(work.keymap.isKeyModified(0, 0), "a key differing from the keyboard's new content was not marked");
    require(work.keymap.isKeyModified(0, 3), "the user's edit lost its mark");
}

void readingTheProfileOnScreenReplacesIt()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    work.keymap.setScanCode(0, 3, 0x0004);
    require(work.unsaved(), "an edit was not noticed");

    auto changed = bytes;
    changed[0] ^= 0x01;
    work.adoptKeyboardProfile(0, changed, "serial");  // the user chose to read it again, edits and all

    require(work.keymap.toBytes() == changed && !work.unsaved() && !work.anyUnsaved(),
            "reading the profile on screen should show the keyboard's content as it is");
    require(!work.keymap.isModified(), "a profile just read counted as edited");
}

void aWrittenProfileStopsCountingAsChanged()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    work.keymap.setScanCode(0, 3, 0x0004);
    work.adoptKeyboardProfile(1, bytes, "serial");
    work.keymap.setScanCode(0, 5, 0x0009);
    require(work.editedProfiles() == (std::vector<std::uint16_t>{0, 1}), "both edits should count");

    work.markWritten(1, work.keymap.toBytes());  // the one on screen
    require(!work.unsaved() && work.editedProfiles() == std::vector<std::uint16_t>{0}, "a written profile still counted");
    work.markWritten(0, work.draft(0)->toBytes());  // the one put aside
    require(!work.anyUnsaved() && work.editedProfiles().empty(), "a written stashed profile still counted");
    require(work.keyboardBytes[0] == work.draft(0)->toBytes(), "what the keyboard holds was not updated");
}

void theFirstUnsavedProfileComesForward()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    work.keymap.setScanCode(0, 3, 0x0004);
    work.adoptKeyboardProfile(1, bytes, "serial");

    require(work.showFirstUnsaved() && work.selected == 0 && work.unsaved(), "the unsaved profile was not shown");
    require(!work.showFirstUnsaved(), "nothing should move when the shown profile has unsaved work");
}

void readingCanDiscardOnlyWhatDidNotComeFromTheKeyboardOrIsTheSameProfile()
{
    ProfileWorkspace work;
    require(!work.readWouldDiscard(1), "nothing on screen cannot be discarded");

    // Content from a file or backup: nothing says which profile it is, so a read replaces it.
    work.keymap = Keymap(keyboardProfile());
    work.loaded = true;
    work.keymap.setScanCode(0, 3, 0x0004);
    require(work.unsaved() && work.readWouldDiscard(1), "edits on content that came from a file were not protected");
    work.savedBytes = work.keymap.toBytes();
    require(!work.readWouldDiscard(1), "saved content should not need a question");

    // Content that came from the keyboard: reading another profile puts it aside, the same one replaces it.
    ProfileWorkspace read;
    read.adoptKeyboardProfile(0, keyboardProfile(), "serial");
    read.keymap.setScanCode(0, 3, 0x0004);
    require(!read.readWouldDiscard(1), "reading another profile loses nothing, so it should not ask");
    require(read.readWouldDiscard(0), "reading the profile on screen again replaces its edits");
}

void aDifferentKeyboardReplacesEverythingReadFromTheFirst()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    require(!work.keyboardChanged("A"), "the first keyboard cannot be a change");
    work.adoptKeyboardProfile(0, bytes, "A");
    require(!work.keyboardChanged("A") && work.keyboardChanged("B"), "a different serial number was not noticed");

    work.adoptKeyboardProfile(1, bytes, "A");  // profile 1 on screen, profile 1 (index 0) put aside clean
    require(work.stashed[0] && !work.keyboardBytes[0].empty(), "the first keyboard's profile was not kept");
    work.adoptKeyboardProfile(0, hhkbs::keymap::Keymap().toBytes(), "B");

    require(work.keyboardSerial == "B", "the new keyboard was not remembered");
    require(!work.stashed[1] && !work.stashed[0], "work put aside for the first keyboard was kept for the second");
    require(work.keyboardBytes[1].empty() && !work.keyboardBytes[0].empty(), "what the first keyboard held was kept");
    require(work.selected == 0 && work.keymap.toBytes() == hhkbs::keymap::Keymap().toBytes(), "the new profile is not on screen");
    require(!work.anyUnsaved(), "a profile just read from the new keyboard counted as unsaved");
}

void unsavedWorkPutAsideIsListed()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "A");
    require(!work.stashedUnsaved() && work.stashedUnsavedList().empty(), "nothing is put aside yet");

    work.keymap.setScanCode(0, 3, 0x0004);
    work.adoptKeyboardProfile(2, bytes, "A");
    require(work.stashedUnsaved() && work.stashedUnsavedList() == "Profile 1", "the profile put aside was not listed");
    work.keymap.setScanCode(0, 5, 0x0009);
    require(work.stashedUnsavedList() == "Profile 1", "the profile on screen is not one put aside");

    work.adoptKeyboardProfile(3, bytes, "A");
    require(work.stashedUnsavedList() == "Profile 1, Profile 3", "both profiles put aside were not listed in order");
}

void aFileOrBackupIsComparedWithTheKeyboard()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    // The keyboard now holds a profile that differs at key 7; the file differs from the profile it was saved from
    // at keys 7 and 9.
    hhkbs::keymap::Keymap held(bytes);
    held.setScanCode(0, 7, 0x0010);
    work.keyboardBytes[0] = held.toBytes();
    hhkbs::keymap::Keymap file(bytes);
    file.setScanCode(0, 7, 0x0010);
    file.setScanCode(0, 9, 0x0022);
    work.keymap = file;
    require(work.keymap.isKeyModified(0, 7) && work.keymap.isKeyModified(0, 9), "the file should differ from its own reference");

    work.useKeyboardAsReference();

    require(!work.keymap.isKeyModified(0, 7), "a key equal to the keyboard's was still marked");
    require(work.keymap.isKeyModified(0, 9), "a key differing from the keyboard's was not marked");
    require(work.keymap.scanCode(0, 7) == 0x0010 && work.keymap.scanCode(0, 9) == 0x0022, "the keys themselves changed");

    // With nothing read from the keyboard for that profile, the content is left as it is.
    ProfileWorkspace unread;
    unread.keymap = file;
    unread.loaded = true;
    unread.useKeyboardAsReference();
    require(unread.keymap.isKeyModified(0, 7), "content was compared with a keyboard that was never read");
}

}  // namespace

int main()
{
    try {
        readingAProfileShowsItClean();
        editsSurviveLeavingAndReturning();
        readingTheProfileOnScreenReplacesIt();
        aWrittenProfileStopsCountingAsChanged();
        theFirstUnsavedProfileComesForward();
        readingCanDiscardOnlyWhatDidNotComeFromTheKeyboardOrIsTheSameProfile();
        aDifferentKeyboardReplacesEverythingReadFromTheFirst();
        unsavedWorkPutAsideIsListed();
        aFileOrBackupIsComparedWithTheKeyboard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
