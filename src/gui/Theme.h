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

// Asks for the desktop's color scheme again; only has an effect in Auto mode. The answer comes from a helper
// program that can be slow, so it is collected later by poll() instead of making the window wait for it.
void refresh();
// Applies the answer to refresh() once it has arrived; cheap enough to call every frame.
void poll();

const Palette& palette();

// How much larger than the plain layout this screen is drawn: 1 on an ordinary display, 2 on one that shows
// everything at twice the size. Sizes written in the layout are multiplied by it with dp().
float scale();
void setScale(float scale);
inline float dp(float value) { return value * scale(); }
inline ImVec2 dp(float x, float y) { return ImVec2(x * scale(), y * scale()); }
}
