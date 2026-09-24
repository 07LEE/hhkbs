#include "gui/KeyboardWidget.h"
#include "keymap/KeyboardLayout.h"
#include "keymap/ScanCodeCatalog.h"
#include <imgui.h>
#include <algorithm>
#include <string>

std::optional<std::size_t> drawKeyboard(const hhkbs::keymap::Keymap& keymap,
                                      std::size_t layer, float height,
                                      const std::string& caption)
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
        const auto background = changed ? IM_COL32(220,234,255,255) :
            hovered ? IM_COL32(226,232,242,255) : gesture ? IM_COL32(244,239,255,255) : IM_COL32(248,249,251,255);
        const auto border = changed ? IM_COL32(43,109,229,255) :
            gesture ? IM_COL32(191,178,225,255) : IM_COL32(199,208,221,255);
        draw->AddRectFilled(top, bottom, background, 6);
        draw->AddRect(top, bottom, border, 6, 0, changed ? 2.f : 1.f);
        const auto& legend = pos.legend;
        const float legendSize = std::clamp(unit * .16f, 9.f, 13.f);
        draw->PushClipRect(top, bottom, true);
        draw->AddText(ImGui::GetFont(), legendSize, ImVec2(top.x+5, top.y+4),
                      IM_COL32(112,121,136,255), legend.c_str());
        const auto label = hhkbs::keymap::ScanCodeCatalog::compactLabelFor(keymap.scanCode(layer, pos.slot));
        float fontSize = std::clamp(unit * .24f, 11.f, 19.f);
        auto extent = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000, 0, label.c_str());
        if (extent.x > size.x-8) fontSize *= (size.x-8)/extent.x;
        extent = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000, 0, label.c_str());
        draw->AddText(ImGui::GetFont(), fontSize,
                      ImVec2(top.x+(size.x-extent.x)/2, top.y+(size.y-extent.y)/2+6),
                      IM_COL32(32,40,53,255), label.c_str());
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
    ImGui::EndChild();
    return activated;
}
