#include "keymap/ProfileFiles.h"
#include "keymap/KeyboardLayout.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

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
        std::filesystem::remove_all(directory);
        std::cout << "Profile file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
