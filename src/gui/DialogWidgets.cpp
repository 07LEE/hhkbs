#include "gui/DialogWidgets.h"
#include "gui/Theme.h"
#include <imgui.h>
#include <algorithm>
#include <vector>

namespace dialog {

void title(const char* text, const char* note)
{
    const float top = ImGui::GetCursorPosY();
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.f);
    if (!note) return;
    // Same trick as the main header: the small text shares the title's baseline.
    const float noteTop = top + ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight() - 2.f;
    ImGui::SameLine(0, 12.f);
    ImGui::SetCursorPosY(noteTop);
    ImGui::TextDisabled("%s", note);
}

void hint(const char* text)
{
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

void error(const std::string& message)
{
    if (message.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.85f, .3f, .3f, 1));
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::PopStyleColor();
}

int footer(std::initializer_list<FooterButton> buttons)
{
    const auto& style = ImGui::GetStyle();
    std::vector<float> widths;
    float total = style.ItemSpacing.x * (buttons.size() - 1);
    for (const auto& button : buttons) {
        widths.push_back(std::max(110.f, ImGui::CalcTextSize(button.label).x + style.FramePadding.x * 2));
        total += widths.back();
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - total);
    const auto& palette = theme::palette();
    int pressed = -1, index = 0;
    for (const auto& button : buttons) {
        if (index) ImGui::SameLine();
        const bool accented = button.primary || button.danger;
        if (accented) {
            ImGui::PushStyleColor(ImGuiCol_Button, button.danger ? ImVec4(.78f, .22f, .22f, 1) : palette.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button.danger ? ImVec4(.85f, .28f, .28f, 1) : palette.accentHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, button.danger ? ImVec4(.68f, .18f, .18f, 1) : palette.accentActive);
            ImGui::PushStyleColor(ImGuiCol_Text, palette.accentText);
        }
        ImGui::BeginDisabled(!button.enabled);
        if (ImGui::Button(button.label, ImVec2(widths[index], 0))) pressed = index;
        ImGui::EndDisabled();
        if (accented) ImGui::PopStyleColor(4);
        ++index;
    }
    return pressed;
}

}
