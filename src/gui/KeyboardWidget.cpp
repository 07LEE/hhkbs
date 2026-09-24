#include "gui/KeyboardWidget.h"
#include "gui/Theme.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ScanCodeCatalog.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {
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
}

std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height,
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
    const float captionHeight = caption.empty() ? 0.f : ImGui::GetTextLineHeight() + 8.f;
    if (captionHeight > 0.f) {
        draw->AddText(ImVec2(start.x + 2, start.y + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), caption.c_str());
        available.y = std::max(1.f, available.y - captionHeight);
    }
    const float unit = std::max(1.0f, std::min(available.x / 17.8f, available.y / 6.1f));
    const ImVec2 origin(start.x + (available.x - unit * 17.8f) / 2,
                        start.y + captionHeight + (available.y - unit * 6.1f) / 2);
    const auto key = [&](const hhkbs::keymap::KeyPosition& pos, bool gesture) {
        const ImVec2 top(origin.x + pos.x * unit + 3, origin.y + pos.y * unit + 3);
        const ImVec2 size(pos.width * unit - 6, unit * .82f - 3);
        const ImVec2 bottom(top.x + size.x, top.y + size.y);
        ImGui::SetCursorScreenPos(top);
        ImGui::PushID(static_cast<int>(pos.slot));
        if (ImGui::InvisibleButton("key", size, ImGuiButtonFlags_EnableNav)) activated = pos.slot;
        const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        const bool changed = keymap.isKeyModified(layer, pos.slot);
        const auto& pal = theme::palette();
        const auto background = changed ? pal.keyChanged :
            hovered ? pal.keyHover : gesture ? pal.keyGesture : pal.keyFill;
        const auto border = changed ? pal.keyBorderChanged :
            gesture ? pal.keyBorderGesture : pal.keyBorder;
        draw->AddRectFilled(top, bottom, background, 6);
        draw->AddRect(top, bottom, border, 6, 0, changed ? 2.f : 1.f);
        const auto& legend = pos.legend;
        const float legendSize = std::clamp(unit * .16f, 9.f, 13.f);
        draw->PushClipRect(top, bottom, true);
        draw->AddText(ImGui::GetFont(), legendSize, ImVec2(top.x+5, top.y+4),
                      pal.keyLegend, legend.c_str());
        const auto label = hhkbs::keymap::ScanCodeCatalog::compactLabelFor(keymap.scanCode(layer, pos.slot));
        // Wrap long names on word boundaries and use the largest size at which they fit the key.
        const float maxWidth = size.x - 8, maxHeight = size.y - 18;
        const float preferred = std::clamp(unit * .24f, 11.f, 19.f);
        auto* font = ImGui::GetFont();
        std::vector<std::string> lines;
        float fontSize = preferred;
        for (; fontSize >= 9.f; fontSize -= .5f) {
            lines = wrapWords(font, fontSize, label, maxWidth);
            if (!lines.empty() && lines.size() * fontSize * 1.15f <= maxHeight) break;
        }
        if (fontSize < 9.f) {
            // Nothing fits at a readable size: keep one line and shrink it to the key width.
            fontSize = 9.f;
            lines = {label};
            const float width = font->CalcTextSizeA(fontSize, 1000, 0, label.c_str()).x;
            if (width > maxWidth) fontSize *= maxWidth / width;
        }
        const float lineHeight = fontSize * 1.15f;
        float y = top.y + 6 + (size.y - 6 - lineHeight * lines.size()) / 2;
        for (const auto& line : lines) {
            const auto extent = font->CalcTextSizeA(fontSize, 1000, 0, line.c_str());
            draw->AddText(font, fontSize, ImVec2(top.x + (size.x - extent.x) / 2, y), pal.keyLabel, line.c_str());
            y += lineHeight;
        }
        draw->PopClipRect();
        if (changed) draw->AddCircleFilled(ImVec2(bottom.x-6, top.y+6), 2.5f, border);
        if (hovered) {
            const auto description = hhkbs::keymap::ScanCodeCatalog::labelFor(keymap.scanCode(layer, pos.slot));
            ImGui::SetTooltip("%s: %s (0x%04X)", legend.c_str(), description.c_str(), keymap.scanCode(layer,pos.slot));
        }
        ImGui::PopID();
    };
    for (const auto& pos : hhkbs::keymap::KeyboardLayout::usStudio()) key(pos, false);
    for (const auto& pos : hhkbs::keymap::KeyboardLayout::gesturePads()) key(pos, true);
    if (!notice.empty()) {
        const float wrap = std::min(205.f, available.x * .22f);
        const auto extent = ImGui::CalcTextSize(notice.c_str(), nullptr, false, wrap);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                      ImVec2(start.x + 2, start.y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y * 2 + 8 - extent.y),
                      ImGui::GetColorU32(noticeMuted ? ImGuiCol_TextDisabled : ImGuiCol_Text), notice.c_str(), nullptr, wrap);
    }
    ImGui::EndChild();
    return activated;
}
