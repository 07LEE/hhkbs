#include "keymap/Keymap.h"

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

}  // namespace

int main()
{
    try {
        profileRoundTripsWithoutDataLoss();
        modificationsCanBeReset();
        invalidInputIsRejected();
    } catch (const std::exception& error) {
        std::cerr << "Keymap test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All keymap tests passed\n";
    return 0;
}
