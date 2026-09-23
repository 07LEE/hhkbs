#pragma once
#include "keymap/Keymap.h"
#include <optional>

// Returns the activated profile slot, including mouse buttons and gesture pads.
std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height);
