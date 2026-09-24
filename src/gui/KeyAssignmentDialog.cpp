#include "gui/KeyAssignmentDialog.h"
#include "gui/Theme.h"
#include "keymap/ScanCodeCatalog.h"
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return std::tolower(c);});
    return s;
}
}
void KeyAssignmentDialog::reset(hhkbs::keymap::Keymap::ScanCode code, std::string keyName) {
    search_.fill(0);
    std::snprintf(raw_.data(), raw_.size(), "0x%04X", code);
    keyName_ = std::move(keyName);
    current_ = code;
    scrollToCurrent_ = true;
}
std::optional<hhkbs::keymap::Keymap::ScanCode> KeyAssignmentDialog::draw(bool& cancelled) {
    const auto& style = ImGui::GetStyle();
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted("Assign key");
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled("%s  /  now: %s (0x%04X)", keyName_.c_str(),
                        hhkbs::keymap::ScanCodeCatalog::labelFor(current_).c_str(), current_);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search keys, categories or hex codes", search_.data(), search_.size());
    const auto needle = lower(search_.data());
    bool accept = false;

    // Entries grouped under their category, in the order the catalog lists them.
    const auto& entries = hhkbs::keymap::ScanCodeCatalog::entries();
    std::vector<std::string> categories;
    for (const auto& entry : entries)
        if (std::find(categories.begin(), categories.end(), entry.category) == categories.end()) categories.push_back(entry.category);

    ImGui::BeginChild("Assignments", ImVec2(0, 290), ImGuiChildFlags_Borders);
    for (const auto& category : categories) {
        bool headerShown = false;
        for (const auto& entry : entries) {
            if (entry.category != category) continue;
            char hex[12];
            std::snprintf(hex, sizeof(hex), "0x%04X", entry.code);
            if (!needle.empty() && lower(category + " " + entry.label + " " + hex).find(needle) == std::string::npos) continue;
            if (!headerShown) { ImGui::SeparatorText(category.c_str()); headerShown = true; }
            ImGui::PushID(entry.code);
            const float rowX = ImGui::GetCursorPosX(), rowWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable(entry.label.c_str(), lower(raw_.data()) == lower(hex), ImGuiSelectableFlags_AllowDoubleClick)) {
                std::snprintf(raw_.data(), raw_.size(), "%s", hex);
                accept = ImGui::IsMouseDoubleClicked(0);
            }
            // The code sits in its own right-hand column, dimmed, on the same line as the name.
            ImGui::SameLine(rowX + rowWidth - ImGui::CalcTextSize(hex).x - 8);
            ImGui::TextDisabled("%s", hex);
            if (scrollToCurrent_ && entry.code == current_) { ImGui::SetScrollHereY(.35f); scrollToCurrent_ = false; }
            ImGui::PopID();
        }
    }
    scrollToCurrent_ = false;  // a code that is not in the catalog has nothing to scroll to
    ImGui::EndChild();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Raw hex code");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##raw", raw_.data(), raw_.size());
    std::string_view value(raw_.data());
    if (value.starts_with("0x") || value.starts_with("0X")) value.remove_prefix(2);
    unsigned code = 0;
    const auto result = std::from_chars(value.data(), value.data()+value.size(), code, 16);
    const bool valid = !value.empty() && value.size() <= 4 && result.ec == std::errc{} &&
                       result.ptr == value.data()+value.size() && code <= 0xFFFF;
    if (!valid) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(.85f, .3f, .3f, 1), "Enter a hexadecimal value from 0000 to FFFF.");
    }

    // Footer: Cancel, then the one primary action, right-aligned like the file dialogs.
    const float buttonWidth = 110.f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - 2 * buttonWidth - style.ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) cancelled = true;
    ImGui::SameLine();
    const auto& palette = theme::palette();
    ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.accentText);
    ImGui::BeginDisabled(!valid);
    accept |= ImGui::Button("Assign", ImVec2(buttonWidth, 0));
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    if (accept && valid) return static_cast<hhkbs::keymap::Keymap::ScanCode>(code);
    return std::nullopt;
}
