#include "gui/KeyboardWidget.h"
#include "gui/Theme.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ScanCodeCatalog.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool sameText(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// Greedy word wrap; returns no lines when a single word is wider than maxWidth.
std::vector<std::string> wrapWords(ImFont* font, float fontSize, const std::string& text, float maxWidth)
{
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;
    const auto width = [&](const std::string& value) { return font->CalcTextSizeA(fontSize, 1000, 0, value.c_str()).x; };
    while (words >> word) {
        if (width(word) > maxWidth) return {};
        const auto candidate = line.empty() ? word : line + " " + word;
        if (line.empty() || width(candidate) <= maxWidth) line = candidate;
        else { lines.push_back(line); line = word; }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// Which gesture pad a slot belongs to: left side, front left, front right, right side.
int padOf(std::size_t slot)
{
    switch (slot) {
    case 86: case 87: return 0;
    case 101: case 102: return 1;
    case 108: case 109: return 2;
    case 116: case 117: return 3;
    default: return -1;
    }
}
}

std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height,
                                      const std::array<std::optional<bool>, 4>& padsOn,
                                      std::optional<std::size_t>& padToggled,
                                      const std::string& caption,
                                      const std::string& notice, bool noticeMuted)
{
    std::optional<std::size_t> activated;
    ImGui::BeginChild("Keyboard", ImVec2(0, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto available = ImGui::GetContentRegionAvail();
    const auto start = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    // Keep the caption clear of the keys by laying the keyboard out below it.
    const float captionHeight = caption.empty() ? 0.f : ImGui::GetTextLineHeight() + theme::dp(8.f);
    if (captionHeight > 0.f) {
        draw->AddText(ImVec2(start.x + theme::dp(2.f), start.y + theme::dp(2.f)), ImGui::GetColorU32(ImGuiCol_TextDisabled), caption.c_str());
        available.y = std::max(1.f, available.y - captionHeight);
    }
    const auto unitFor = [&] { return std::max(1.0f, std::min(available.x / 17.8f, available.y / 6.1f)); };
    // The notice sits at the bottom of the frame. When the keys would reach down to it, as in a low window, they are
    // laid out above it instead.
    if (!notice.empty()) {
        const float noticeBlock = ImGui::CalcTextSize(notice.c_str(), nullptr, false, std::max(1.f, available.x - theme::dp(4.f))).y
                                  + theme::dp(8.f);
        if (available.y - unitFor() * 6.1f < 2 * noticeBlock) available.y = std::max(1.f, available.y - noticeBlock);
    }
    const float unit = unitFor();
    const ImVec2 origin(start.x + (available.x - unit * 17.8f) / 2,
                        start.y + captionHeight + (available.y - unit * 6.1f) / 2);
    const auto key = [&](const hhkbs::keymap::KeyPosition& pos, bool gesture) {
        const int pad = gesture ? padOf(pos.slot) : -1;
        const bool padOff = pad >= 0 && padsOn[pad] == false;
        const ImVec2 top(origin.x + pos.x * unit + theme::dp(3.f), origin.y + pos.y * unit + theme::dp(3.f));
        const ImVec2 size(pos.width * unit - theme::dp(6.f), unit * .82f - theme::dp(3.f));
        const ImVec2 bottom(top.x + size.x, top.y + size.y);
        ImGui::SetCursorScreenPos(top);
        ImGui::PushID(static_cast<int>(pos.slot));
        if (ImGui::InvisibleButton("key", size, ImGuiButtonFlags_EnableNav)) activated = pos.slot;
        // A key that kept the focus after a click keeps no tooltip; only moving with the keyboard shows it by focus.
        const bool hovered = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
        const bool changed = keymap.isKeyModified(layer, pos.slot);
        const auto& pal = theme::palette();
        const auto background = changed ? pal.keyChanged :
            hovered ? pal.keyHover : gesture ? pal.keyGesture : pal.keyFill;
        const auto border = changed ? pal.keyBorderChanged :
            gesture ? pal.keyBorderGesture : pal.keyBorder;
        draw->AddRectFilled(top, bottom, background, theme::dp(6.f));
        draw->AddRect(top, bottom, border, theme::dp(6.f), 0, theme::dp(changed ? 2.f : 1.f));
        const auto& legend = pos.legend;
        const float legendSize = std::clamp(unit * .16f, theme::dp(9.f), theme::dp(13.f));
        draw->PushClipRect(top, bottom, true);
        draw->AddText(ImGui::GetFont(), legendSize, ImVec2(top.x + theme::dp(5.f), top.y + theme::dp(4.f)),
                      pal.keyLegend, legend.c_str());
        const auto label = hhkbs::keymap::ScanCodeCatalog::compactLabelFor(keymap.scanCode(layer, pos.slot));
        // Wrap long names on word boundaries and use the largest size at which they fit the key.
        const float maxWidth = size.x - theme::dp(8.f), maxHeight = size.y - theme::dp(18.f);
        const float preferred = std::clamp(unit * .24f, theme::dp(11.f), theme::dp(19.f));
        auto* font = ImGui::GetFont();
        std::vector<std::string> lines;
        float fontSize = preferred;
        for (; fontSize >= theme::dp(9.f); fontSize -= theme::dp(.5f)) {
            lines = wrapWords(font, fontSize, label, maxWidth);
            if (!lines.empty() && lines.size() * fontSize * 1.15f <= maxHeight) break;
        }
        if (fontSize < theme::dp(9.f)) {
            // Nothing fits at a readable size: keep one line and shrink it to the key width.
            fontSize = theme::dp(9.f);
            lines = {label};
            const float width = font->CalcTextSizeA(fontSize, 1000, 0, label.c_str()).x;
            if (width > maxWidth) fontSize *= maxWidth / width;
        }
        const float lineHeight = fontSize * 1.15f;
        float y = top.y + theme::dp(6.f) + (size.y - theme::dp(6.f) - lineHeight * lines.size()) / 2;
        for (const auto& line : lines) {
            const auto extent = font->CalcTextSizeA(fontSize, 1000, 0, line.c_str());
            draw->AddText(font, fontSize, ImVec2(top.x + (size.x - extent.x) / 2, y), pal.keyLabel, line.c_str());
            y += lineHeight;
        }
        if (padOff) draw->AddRectFilled(top, bottom, ImGui::GetColorU32(ImGuiCol_WindowBg, .55f), theme::dp(6.f));
        draw->PopClipRect();
        if (changed) draw->AddCircleFilled(ImVec2(bottom.x - theme::dp(6.f), top.y + theme::dp(6.f)), theme::dp(2.5f), border);
        if (hovered) {
            const auto code = keymap.scanCode(layer, pos.slot);
            const auto description = hhkbs::keymap::ScanCodeCatalog::labelFor(code);
            // "J: J" says nothing twice, so a key that still sends its own legend shows the name once.
            if (sameText(legend, description)) ImGui::SetTooltip("%s (0x%04X)", description.c_str(), code);
            else ImGui::SetTooltip("%s: %s (0x%04X)", legend.c_str(), description.c_str(), code);
        }
        ImGui::PopID();
    };
    for (const auto& pos : hhkbs::keymap::KeyboardLayout::usStudio()) key(pos, false);
    for (const auto& pos : hhkbs::keymap::KeyboardLayout::gesturePads()) key(pos, true);
    // An On/Off button by each pad: below a side pad, outside a front pad.
    struct PadTag { float x, y, width; };
    const std::array<PadTag, 4> tags{{{0.f, 3.3f, 1.15f}, {2.4f, 5.25f, 1.1f}, {14.25f, 5.25f, 1.1f}, {16.65f, 3.3f, 1.15f}}};
    const char* padNames[] = {"Left side", "Front left", "Front right", "Right side"};
    for (std::size_t pad = 0; pad < tags.size(); ++pad) {
        const auto& tag = tags[pad];
        const bool known = padsOn[pad].has_value();
        const bool on = padsOn[pad].value_or(true);
        const ImVec2 top(origin.x + tag.x * unit + theme::dp(3.f), origin.y + tag.y * unit + theme::dp(3.f));
        const ImVec2 size(tag.width * unit - theme::dp(6.f), unit * .82f - theme::dp(3.f));
        const ImVec2 bottom(top.x + size.x, top.y + size.y);
        ImGui::SetCursorScreenPos(top);
        ImGui::PushID(static_cast<int>(pad) + 1000);
        // The pad changes when the button is released, like any other, and can be reached and pressed from the keyboard.
        const bool pressed = ImGui::InvisibleButton("pad", size, ImGuiButtonFlags_EnableNav);
        if (known && pressed) padToggled = pad;
        const bool hovered = known && ImGui::IsItemHovered();
        ImGui::PopID();
        const auto& pal = theme::palette();
        draw->AddRectFilled(top, bottom, hovered ? pal.keyHover : pal.keyFill, theme::dp(6.f));
        draw->AddRect(top, bottom, on ? pal.keyBorderGesture : pal.keyBorder, theme::dp(6.f));
        const char* text = on ? "On" : "Off";
        const auto extent = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(top.x + (size.x - extent.x) / 2, top.y + (size.y - extent.y) / 2),
                      on ? pal.keyLabel : ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        if (ImGui::IsMouseHoveringRect(top, bottom) && ImGui::IsWindowHovered()) {
            if (known) ImGui::SetTooltip("%s pad: %s (click to turn %s)", padNames[pad], on ? "on" : "off", on ? "off" : "on");
            else ImGui::SetTooltip("%s pad: connect a keyboard to change it", padNames[pad]);
        }
    }
    if (!notice.empty()) {
        const float wrap = std::max(1.f, available.x - theme::dp(4.f));
        const auto extent = ImGui::CalcTextSize(notice.c_str(), nullptr, false, wrap);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                      ImVec2(start.x + theme::dp(2.f), start.y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y * 2 + theme::dp(8.f) - extent.y),
                      ImGui::GetColorU32(noticeMuted ? ImGuiCol_TextDisabled : ImGuiCol_Text), notice.c_str(), nullptr, wrap);
    }
    ImGui::EndChild();
    return activated;
}
