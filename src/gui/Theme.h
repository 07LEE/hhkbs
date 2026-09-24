#pragma once
#include <imgui.h>

namespace theme {
// Colors that the widgets draw directly instead of taking them from the ImGui style.
struct Palette {
    ImVec4 selected;      // chosen layer / profile button
    ImVec4 accent, accentHovered, accentActive, accentText;
    ImU32 keyFill, keyHover, keyChanged, keyGesture;
    ImU32 keyBorder, keyBorderChanged, keyBorderGesture;
    ImU32 keyLegend, keyLabel;
    ImVec4 window;
};

enum class Mode { Auto, Light, Dark };

// HHKBS_THEME=light|dark forces a mode; otherwise the desktop's color scheme decides.
Mode requestedMode();
bool systemPrefersDark();

void apply(bool dark);
const Palette& palette();
}
