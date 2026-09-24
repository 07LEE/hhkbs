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

// The mode starts from HHKBS_THEME=light|dark (default Auto: follow the desktop).
Mode mode();
void setMode(Mode mode);
const char* modeName(Mode mode);
Mode nextMode(Mode mode);

// Re-reads the desktop's color scheme; only has an effect in Auto mode.
void refresh();

const Palette& palette();
}
