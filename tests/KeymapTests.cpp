#include "keymap/KeyboardLayout.h"
#include "keymap/Keymap.h"
#include "keymap/ProfileDiff.h"
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
    require(profile.scanCode(2, 16) == 0x0014, "Fn2 Q should keep its letter by default");
    require(profile.scanCode(2, 36) == 0x000B, "Fn2 H should keep its letter by default");
    require(profile.scanCode(2, 35) == 0x5FA2, "Fn2 pointer speed increase default is incorrect");
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

void tomlCommentsAndStringsAreNotSyntax()
{
    Keymap original(hhkbs::keymap::KeyboardLayout::demoProfile());
    original.setScanCode(1, 7, 0x0123);
    auto document = hhkbs::keymap::ProfileSerializer::toToml(original);

    const auto replaceFirst = [&](const std::string& from, const std::string& to) {
        const auto at = document.find(from);
        require(at != std::string::npos, "test document is missing the text to replace");
        document.replace(at, from.size(), to);
    };
    replaceFirst("[[layers]]", "[[layers]] # the first ] layer\n");
    replaceFirst("scancodes = [\n", "scancodes = [ # note ] [[layers]]\n  # 0x7777, ]\n");
    document = "# [[layers]] old configuration\ntitle = \"a ] [[layers]] \\\" b\"\nnote = 'scancodes = ['\n" + document;

    const auto parsed = hhkbs::keymap::ProfileSerializer::fromToml(document);
    require(parsed.toBytes() == original.toBytes(), "a comment or string was read as TOML syntax");
}

// One [[layers]] table holding `count` codes, each written as `code`.
std::string layerTable(const std::size_t count, const std::string& code = "0x0004")
{
    std::string table = "[[layers]]\nscancodes = [\n";
    for (std::size_t index = 0; index < count; ++index) table += "  " + code + ",\n";
    return table + "]\n";
}

std::string document(const std::size_t layers, const std::size_t codesPerLayer = Keymap::keysPerLayer,
                     const std::string& code = "0x0004")
{
    std::string text;
    for (std::size_t layer = 0; layer < layers; ++layer) text += layerTable(codesPerLayer, code) + "\n";
    return text;
}

// What the reader says about a document it refuses; empty when it accepts it.
std::string refusal(const std::string& text)
{
    try {
        static_cast<void>(hhkbs::keymap::ProfileSerializer::fromToml(text));
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    return {};
}

bool rejected(const std::string& text) { return !refusal(text).empty(); }

void malformedTomlIsRejected()
{
    requireThrows<std::invalid_argument>(
        [] {
            static_cast<void>(
                hhkbs::keymap::ProfileSerializer::fromToml(
                    "[[layers]]\nscancodes = [0x0004]\n"));
        },
        "incomplete TOML profile was accepted");

    require(!rejected(document(Keymap::layerCount)), "a well formed profile was refused");
    require(rejected(""), "an empty document was accepted");
    require(rejected("# only a comment\n"), "a document without layers was accepted");
    // The reader says what is wrong, since the person who wrote the file only has that message to go on.
    require(refusal(document(Keymap::layerCount - 1)).find("four layers") != std::string::npos, "a missing layer was not named");
    require(refusal(document(Keymap::layerCount + 1)).find("four layers") != std::string::npos, "an extra layer was not named");
    require(refusal(document(Keymap::layerCount, Keymap::keysPerLayer - 1)).find("120 scan codes") != std::string::npos,
            "a missing code was not named");
    require(refusal(document(Keymap::layerCount, Keymap::keysPerLayer + 1)).find("120 scan codes") != std::string::npos,
            "an extra code was not named");

    std::string noCodes = document(Keymap::layerCount - 1) + "[[layers]]\n";
    require(rejected(noCodes), "a layer without scancodes was accepted");
    std::string unclosed = document(Keymap::layerCount - 1) + "[[layers]]\nscancodes = [\n  0x0004,\n";
    require(rejected(unclosed), "an array that is never closed was accepted");

    require(rejected(document(Keymap::layerCount, Keymap::keysPerLayer, "0xZZ")), "a code that is not hexadecimal was accepted");
    require(rejected(document(Keymap::layerCount, Keymap::keysPerLayer, "0x10000")), "a code beyond 16 bits was accepted");
    require(rejected(document(Keymap::layerCount, Keymap::keysPerLayer, "-1")), "a negative code was accepted");
    require(rejected(document(Keymap::layerCount, Keymap::keysPerLayer, "\"0x0004\"")), "a quoted code was accepted");
    require(rejected(document(Keymap::layerCount, Keymap::keysPerLayer, "0x00 04")), "a code with a space inside was accepted");
}

void tomlAcceptsTheFormsPeopleWrite()
{
    Keymap original(hhkbs::keymap::KeyboardLayout::demoProfile());
    original.setScanCode(0, 0, 0x0000);
    original.setScanCode(0, 1, 0xFFFF);
    original.setScanCode(3, 119, 0x5FA4);
    const auto written = hhkbs::keymap::ProfileSerializer::toToml(original);

    // Line endings from another system.
    std::string windows;
    for (const char c : written) windows += c == '\n' ? std::string("\r\n") : std::string(1, c);
    require(hhkbs::keymap::ProfileSerializer::fromToml(windows).toBytes() == original.toBytes(),
            "a profile with Windows line endings was not read");

    // The extremes survive.
    const auto back = hhkbs::keymap::ProfileSerializer::fromToml(written);
    require(back.scanCode(0, 0) == 0x0000 && back.scanCode(0, 1) == 0xFFFF && back.scanCode(3, 119) == 0x5FA4,
            "the lowest and highest codes did not round trip");

    // Decimal, upper case prefix and a missing trailing comma are all ordinary ways to write a number list.
    std::string decimal = "[[layers]]\nscancodes = [";
    for (std::size_t index = 0; index < Keymap::keysPerLayer; ++index) decimal += (index ? ", " : "") + std::to_string(index % 200);
    decimal += "]\n";
    std::string text = decimal + "\n" + document(Keymap::layerCount - 1, Keymap::keysPerLayer, "0X00A0");
    const auto parsed = hhkbs::keymap::ProfileSerializer::fromToml(text);
    require(parsed.scanCode(0, 7) == 7 && parsed.scanCode(0, 119) == 119, "decimal codes were not read");
    require(parsed.scanCode(1, 0) == 0x00A0 && parsed.scanCode(3, 119) == 0x00A0, "an upper case 0X prefix was not read");

    // Other tables and keys around the layers are left alone.
    const auto withExtras = "title = \"my profile\"\n\n" + document(Keymap::layerCount) + "[other]\nvalue = 1\n";
    require(!rejected(withExtras), "keys and tables other than the layers should be ignored");
}

}  // namespace

void rebaseMovesTheReferenceNotTheKeys()
{
    Keymap keymap;
    keymap.setScanCode(0, 3, 0x0004);

    Keymap keyboard;
    keyboard.setScanCode(0, 3, 0x0004);
    keymap.rebase(keyboard.toBytes());
    require(keymap.scanCode(0, 3) == 0x0004, "rebase must keep the edited keys");
    require(!keymap.isModified(), "keys equal to the new reference are not modified");

    keymap.setScanCode(0, 5, 0x0009);
    require(keymap.isKeyModified(0, 5) && !keymap.isKeyModified(0, 3), "only keys off the reference are modified");
    keymap.reset();
    require(keymap.scanCode(0, 5) == 0 && keymap.scanCode(0, 3) == 0x0004, "reset must return to the new reference");

    requireThrows<std::invalid_argument>([&] { keymap.rebase({1, 2, 3}); }, "a short profile must be rejected");
}

void diffListsOnlyTheChangedKeys()
{
    Keymap before;
    Keymap after;
    require(hhkbs::keymap::diffProfiles(before.layers(), after.layers()).empty(),
            "identical profiles must have no changes");

    after.setScanCode(2, 7, 0x0004);
    after.setScanCode(0, 9, 0x0005);
    before.setScanCode(0, 9, 0x0001);

    const auto changes = hhkbs::keymap::diffProfiles(before.layers(), after.layers());
    require(changes.size() == 2, "expected exactly the two edited keys");
    require(changes[0].layer == 0 && changes[0].slot == 9, "changes must come in layer then slot order");
    require(changes[0].before == 0x0001 && changes[0].after == 0x0005, "a change must carry both values");
    require(changes[1].layer == 2 && changes[1].slot == 7 && changes[1].before == 0 && changes[1].after == 0x0004,
            "a change on a later layer was reported incorrectly");
}

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
        tomlCommentsAndStringsAreNotSyntax();
        malformedTomlIsRejected();
        tomlAcceptsTheFormsPeopleWrite();
        diffListsOnlyTheChangedKeys();
        rebaseMovesTheReferenceNotTheKeys();
    } catch (const std::exception& error) {
        std::cerr << "Keymap test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All keymap tests passed\n";
    return 0;
}
