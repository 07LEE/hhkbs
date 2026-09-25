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

void pinFooter()
{
    const auto& style = ImGui::GetStyle();
    // footer() starts with a Spacing(), then one row of buttons.
    const float top = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight() - style.ItemSpacing.y;
    if (ImGui::GetCursorPosY() < top) ImGui::SetCursorPosY(top);
}

namespace {

float buttonWidth(const FooterButton& button)
{
    return std::max(110.f, ImGui::CalcTextSize(button.label).x + ImGui::GetStyle().FramePadding.x * 2);
}

// One footer button; returns whether it was pressed.
bool drawButton(const FooterButton& button)
{
    const auto& palette = theme::palette();
    const bool accented = button.primary || button.danger;
    if (accented) {
        ImGui::PushStyleColor(ImGuiCol_Button, button.danger ? ImVec4(.78f, .22f, .22f, 1) : palette.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button.danger ? ImVec4(.85f, .28f, .28f, 1) : palette.accentHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, button.danger ? ImVec4(.68f, .18f, .18f, 1) : palette.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, palette.accentText);
    }
    ImGui::BeginDisabled(!button.enabled);
    const bool pressed = ImGui::Button(button.label, ImVec2(buttonWidth(button), 0));
    ImGui::EndDisabled();
    if (accented) ImGui::PopStyleColor(4);
    return pressed;
}

}  // namespace

int footer(std::initializer_list<FooterButton> buttons)
{
    const auto& style = ImGui::GetStyle();
    float total = style.ItemSpacing.x * (buttons.size() - 1);
    for (const auto& button : buttons) total += buttonWidth(button);
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - total);
    int pressed = -1, index = 0;
    for (const auto& button : buttons) {
        if (index) ImGui::SameLine();
        if (drawButton(button)) pressed = index;
        ++index;
    }
    return pressed;
}

int footer(std::initializer_list<FooterButton> left, std::initializer_list<FooterButton> right)
{
    const auto& style = ImGui::GetStyle();
    float rightTotal = right.size() ? style.ItemSpacing.x * (right.size() - 1) : 0.f;
    for (const auto& button : right) rightTotal += buttonWidth(button);
    ImGui::Spacing();
    ImGui::SetCursorPosX(style.WindowPadding.x);
    int pressed = -1, index = 0;
    for (const auto& button : left) {
        if (index) ImGui::SameLine();
        if (drawButton(button)) pressed = index;
        ++index;
    }
    const float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightTotal;
    bool first = true;
    for (const auto& button : right) {
        if (!first) ImGui::SameLine();
        else if (left.size()) ImGui::SameLine(rightX);
        else ImGui::SetCursorPosX(rightX);
        first = false;
        if (drawButton(button)) pressed = index;
        ++index;
    }
    return pressed;
}

}
