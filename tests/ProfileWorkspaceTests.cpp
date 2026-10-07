#include "app/ProfileWorkspace.h"
#include "keymap/KeyboardLayout.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using hhkbs::app::ProfileWorkspace;

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

void aFileOrBackupIsComparedWithTheKeyboard()
{
    ProfileWorkspace work;
    const auto bytes = keyboardProfile();
    work.adoptKeyboardProfile(0, bytes, "serial");
    hhkbs::keymap::Keymap other(bytes);
    other.setScanCode(0, 7, 0x0010);
    work.keymap = other;
    work.useKeyboardAsReference();
    require(work.keymap.isKeyModified(0, 7), "the key differing from the keyboard was not marked");
}

}  // namespace

int main()
{
    try {
        readingAProfileShowsItClean();
        editsSurviveLeavingAndReturning();
        aWrittenProfileStopsCountingAsChanged();
        theFirstUnsavedProfileComesForward();
        aFileOrBackupIsComparedWithTheKeyboard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
