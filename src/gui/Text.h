#pragma once
#include <string>

// Small text helpers the interface code shares.
namespace text {

// `value` in lower case (ASCII letters only).
std::string lower(std::string value);

// What a shell command printed on its standard output; empty when it could not run or did not succeed.
std::string commandOutput(const char* command);

}
