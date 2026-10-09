#include "gui/Text.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace text {

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string commandOutput(const char* command)
{
    std::string output;
    if (FILE* pipe = ::popen(command, "r")) {
        std::array<char, 1024> buffer{};
        while (const auto count = std::fread(buffer.data(), 1, buffer.size(), pipe))
            output.append(buffer.data(), count);
        if (::pclose(pipe) != 0) output.clear();
    }
    return output;
}

}
