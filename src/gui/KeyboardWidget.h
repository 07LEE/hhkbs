#pragma once
#include "keymap/Keymap.h"
#include <optional>
#include <string>

// Returns the activated profile slot, including mouse buttons and gesture pads.
// The caption is drawn in the top-left corner of the frame. The notice is drawn in the bottom-left corner,
// over space the keys leave empty, so showing or clearing it never changes the frame's size.
std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height,
                                      const std::string& caption = {},
                                      const std::string& notice = {}, bool noticeMuted = false);
