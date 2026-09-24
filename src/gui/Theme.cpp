#include "gui/Theme.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace theme {
namespace {
Palette current;

std::string run(const char* command)
{
    std::string output;
    if (FILE* pipe = ::popen(command, "r")) {
        std::array<char, 256> buffer{};
        while (const auto count = std::fread(buffer.data(), 1, buffer.size(), pipe))
            output.append(buffer.data(), count);
        if (::pclose(pipe) != 0) output.clear();
    }
    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return output;
}

bool contains(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }
}

Mode requestedMode()
{
    const char* value = std::getenv("HHKBS_THEME");
    if (!value) return Mode::Auto;
    const std::string_view text(value);
    if (text == "dark") return Mode::Dark;
    if (text == "light") return Mode::Light;
    return Mode::Auto;
}

bool systemPrefersDark()
{
    // GNOME, and any desktop whose XDG portal mirrors org.gnome.desktop.interface.
    const auto scheme = run("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
    if (contains(scheme, "prefer-dark")) return true;
    if (contains(scheme, "prefer-light") || contains(scheme, "default")) return false;
    // Older or non-GNOME setups: fall back to the GTK theme name.
    if (const char* gtk = std::getenv("GTK_THEME")) {
        std::string name(gtk);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return contains(name, "dark");
    }
    return contains(run("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null"), "dark");
}

void apply(bool dark)
{
    if (dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(24,24);
    style.FramePadding = ImVec2(12,8);
    style.ItemSpacing = ImVec2(10,10);
    style.FrameRounding = 5;
    style.ChildRounding = 10;
    style.WindowRounding = 8;
    auto& c = style.Colors;
    if (dark) {
        current.window = ImVec4(.09f,.10f,.12f,1);
        c[ImGuiCol_WindowBg] = current.window;
        c[ImGuiCol_PopupBg] = ImVec4(.13f,.14f,.17f,1);
        c[ImGuiCol_ChildBg] = ImVec4(0,0,0,0);
        c[ImGuiCol_Border] = ImVec4(.28f,.31f,.37f,1);
        c[ImGuiCol_Text] = ImVec4(.90f,.92f,.95f,1);
        c[ImGuiCol_TextDisabled] = ImVec4(.50f,.54f,.61f,1);
        c[ImGuiCol_Button] = ImVec4(.18f,.20f,.25f,1);
        c[ImGuiCol_ButtonHovered] = ImVec4(.24f,.27f,.33f,1);
        c[ImGuiCol_ButtonActive] = ImVec4(.29f,.33f,.40f,1);
        c[ImGuiCol_FrameBg] = ImVec4(.14f,.16f,.20f,1);
        c[ImGuiCol_Separator] = ImVec4(.28f,.31f,.37f,1);
        current.selected = ImVec4(.20f,.36f,.64f,1);
        current.accent = ImVec4(.25f,.52f,.96f,1);
        current.accentHovered = ImVec4(.32f,.58f,1.0f,1);
        current.accentActive = ImVec4(.20f,.46f,.90f,1);
        current.accentText = ImVec4(1,1,1,1);
        current.keyFill = IM_COL32(28,31,38,255);
        current.keyHover = IM_COL32(42,47,58,255);
        current.keyChanged = IM_COL32(28,52,92,255);
        current.keyGesture = IM_COL32(43,36,64,255);
        current.keyBorder = IM_COL32(64,71,86,255);
        current.keyBorderChanged = IM_COL32(96,156,255,255);
        current.keyBorderGesture = IM_COL32(114,98,160,255);
        current.keyLegend = IM_COL32(139,148,164,255);
        current.keyLabel = IM_COL32(230,234,241,255);
    } else {
        current.window = ImVec4(.97f,.98f,.99f,1);
        c[ImGuiCol_WindowBg] = current.window;
        c[ImGuiCol_Button] = ImVec4(.88f,.91f,.95f,1);
        current.selected = ImVec4(.69f,.80f,.97f,1);
        current.accent = ImVec4(.25f,.52f,.96f,1);
        current.accentHovered = ImVec4(.20f,.46f,.90f,1);
        current.accentActive = ImVec4(.16f,.40f,.84f,1);
        current.accentText = ImVec4(1,1,1,1);
        current.keyFill = IM_COL32(248,249,251,255);
        current.keyHover = IM_COL32(226,232,242,255);
        current.keyChanged = IM_COL32(220,234,255,255);
        current.keyGesture = IM_COL32(244,239,255,255);
        current.keyBorder = IM_COL32(199,208,221,255);
        current.keyBorderChanged = IM_COL32(43,109,229,255);
        current.keyBorderGesture = IM_COL32(191,178,225,255);
        current.keyLegend = IM_COL32(112,121,136,255);
        current.keyLabel = IM_COL32(32,40,53,255);
    }
}

const Palette& palette() { return current; }
}
