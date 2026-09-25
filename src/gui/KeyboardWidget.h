#pragma once
#include "keymap/Keymap.h"
#include <array>
#include <optional>
#include <string>

// Returns the activated profile slot, including mouse buttons and gesture pads.
// padsOn holds what the keyboard says for each gesture pad (left side, front left, front right, right side).
// A pad with no value is drawn as On (the default) and cannot be clicked. Clicking a known pad's tag sets
// padToggled to that pad; the caller switches it. A pad that is off is drawn dimmed.
// The caption is drawn in the top-left corner of the frame. The notice is drawn in the bottom-left corner,
// over space the keys leave empty, so showing or clearing it never changes the frame's size.
std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height,
                                      const std::array<std::optional<bool>, 4>& padsOn,
                                      std::optional<std::size_t>& padToggled,
                                      const std::string& caption = {},
                                      const std::string& notice = {}, bool noticeMuted = false);
