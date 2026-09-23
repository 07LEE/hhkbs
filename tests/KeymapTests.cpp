#include "keymap/KeyboardLayout.h"
#include "keymap/Keymap.h"
#include "keymap/ProfileSerializer.h"
#include "keymap/ScanCodeCatalog.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using hhkbs::keymap::Keymap;

void require(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<typename Exception, typename Function>
void requireThrows(Function&& function, const std::string& message)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void profileRoundTripsWithoutDataLoss()
{
    std::vector<std::uint8_t> profile(Keymap::profileByteCount);
    for (std::size_t index = 0; index < profile.size(); ++index) {
        profile[index] = static_cast<std::uint8_t>(index & 0xFFU);
    }

    const Keymap keymap(profile);

    require(keymap.scanCode(0, 0) == 0x0001, "first scan code was decoded incorrectly");
    require(keymap.scanCode(1, 0) == 0xF0F1, "layer offset was decoded incorrectly");
    require(keymap.toBytes() == profile, "profile did not survive a decode/encode round trip");
    require(!keymap.isModified(), "a freshly loaded profile must not be marked modified");
}

void modificationsCanBeReset()
{
    const std::vector<std::uint8_t> profile(Keymap::profileByteCount, 0);
    Keymap keymap(profile);

    keymap.setScanCode(3, Keymap::keysPerLayer - 1, 0xABCD);

    require(keymap.isModified(), "a changed scan code was not tracked");
    require(
        keymap.scanCode(3, Keymap::keysPerLayer - 1) == 0xABCD,
        "the changed scan code was not stored");

    keymap.reset();

    require(!keymap.isModified(), "reset did not clear the modified state");
    require(
        keymap.scanCode(3, Keymap::keysPerLayer - 1) == 0,
        "reset did not restore the original scan code");
}

void invalidInputIsRejected()
{
    requireThrows<std::invalid_argument>(
        [] { Keymap(std::vector<std::uint8_t>(Keymap::profileByteCount - 1)); },
        "a short profile was accepted");

    Keymap keymap;
    requireThrows<std::out_of_range>(
        [&keymap] { static_cast<void>(keymap.scanCode(Keymap::layerCount, 0)); },
        "an invalid layer index was accepted");
    requireThrows<std::out_of_range>(
        [&keymap] { keymap.setScanCode(0, Keymap::keysPerLayer, 1); },
        "an invalid key index was accepted");
}

void individualChangesAreTracked()
{
    const std::vector<std::uint8_t> profile(Keymap::profileByteCount, 0);
    Keymap keymap(profile);
    keymap.setScanCode(2, 17, 0x004F);

    require(keymap.isKeyModified(2, 17), "changed key was not tracked");
    require(!keymap.isKeyModified(2, 18), "unchanged key was marked as changed");
}

void studioLayoutHasUniqueEditableSlots()
{
    const auto& layout = hhkbs::keymap::KeyboardLayout::usStudio();
    require(layout.size() == 63, "US Studio layout must expose 60 keys and 3 mouse buttons");

    std::vector<std::size_t> slots;
    for (const auto& position : layout) {
        require(position.slot < Keymap::keysPerLayer, "layout slot is outside the profile");
        slots.push_back(position.slot);
    }
    std::ranges::sort(slots);
    require(
        std::ranges::adjacent_find(slots) == slots.end(),
        "layout contains duplicate profile slots");
}

void gesturePadLayoutHasAllDirections()
{
    const auto& pads = hhkbs::keymap::KeyboardLayout::gesturePads();
    require(pads.size() == 8, "four gesture pads must expose eight directions");

    std::vector<std::size_t> slots;
    for (const auto& direction : pads) {
        require(direction.slot < Keymap::keysPerLayer, "gesture slot is outside the profile");
        slots.push_back(direction.slot);
    }
    std::ranges::sort(slots);
    require(
        std::ranges::adjacent_find(slots) == slots.end(),
        "gesture pad layout contains duplicate slots");

    for (const auto& key : hhkbs::keymap::KeyboardLayout::usStudio()) {
        require(
            std::ranges::find(slots, key.slot) == slots.end(),
            "gesture pad slot overlaps a keyboard slot");
    }
}

void factoryProfileContainsAllDefaultLayers()
{
    const Keymap profile(hhkbs::keymap::KeyboardLayout::usWindowsFactoryProfile());

    require(profile.scanCode(0, 0) == 0x0029, "base Escape default is incorrect");
    require(profile.scanCode(0, 79) == 0x00F4, "mouse left default is incorrect");
    require(profile.scanCode(0, 86) == 0x0052, "left gesture pad default is incorrect");
    require(profile.scanCode(0, 108) == 0x5F8C, "front gesture pad default is incorrect");
    require(profile.scanCode(1, 1) == 0x003A, "Fn1 F1 default is incorrect");
    require(profile.scanCode(1, 31) == 0x00AA, "Fn1 volume default is incorrect");
    require(profile.scanCode(2, 1) == 0x5FA4, "Fn2 pointer speed default is incorrect");
    require(profile.scanCode(2, 36) == 0x00F5, "Fn2 mouse click default is incorrect");
    require(profile.scanCode(3, 0) == 0x0029, "Fn3 base default is incorrect");
}

void tomlProfilesRoundTrip()
{
    Keymap original(hhkbs::keymap::KeyboardLayout::demoProfile());
    original.setScanCode(3, 119, 0xABCD);

    const auto document = hhkbs::keymap::ProfileSerializer::toToml(original);
    const auto parsed = hhkbs::keymap::ProfileSerializer::fromToml(document);

    require(parsed.toBytes() == original.toBytes(), "TOML profile did not round trip");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0x004F) == "Right Arrow",
        "known scan code label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0xABCD) == "0xABCD",
        "unknown scan code label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::compactLabelFor(0x0036) == ",",
        "comma compact label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::compactLabelFor(0x0034) == "'",
        "apostrophe compact label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0x00F4) == "Mouse Left Click",
        "HHKB Studio mouse code label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0x5FA7) == "Pointer Speed 4",
        "HHKB Studio device function label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0x00F9) == "Wheel Up",
        "gesture pad wheel label is incorrect");
    require(
        hhkbs::keymap::ScanCodeCatalog::labelFor(0x5F8D) == "Next Window",
        "gesture pad window-switch label is incorrect");
}

void malformedTomlIsRejected()
{
    requireThrows<std::invalid_argument>(
        [] {
            static_cast<void>(
                hhkbs::keymap::ProfileSerializer::fromToml(
                    "[[layers]]\nscancodes = [0x0004]\n"));
        },
        "incomplete TOML profile was accepted");
}

}  // namespace

int main()
{
    try {
        profileRoundTripsWithoutDataLoss();
        modificationsCanBeReset();
        invalidInputIsRejected();
        individualChangesAreTracked();
        studioLayoutHasUniqueEditableSlots();
        gesturePadLayoutHasAllDirections();
        factoryProfileContainsAllDefaultLayers();
        tomlProfilesRoundTrip();
        malformedTomlIsRejected();
    } catch (const std::exception& error) {
        std::cerr << "Keymap test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All keymap tests passed\n";
    return 0;
}
