#include "gui/KeyAssignmentDialog.h"
#include "keymap/ScanCodeCatalog.h"
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <string>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return std::tolower(c);});
    return s;
}
}
void KeyAssignmentDialog::reset(hhkbs::keymap::Keymap::ScanCode code) {
    search_.fill(0);
    std::snprintf(raw_.data(), raw_.size(), "0x%04X", code);
}
std::optional<hhkbs::keymap::Keymap::ScanCode> KeyAssignmentDialog::draw() {
    ImGui::TextUnformatted("Choose a key or device function.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search keys, categories or hex codes", search_.data(), search_.size());
    const auto needle = lower(search_.data());
    bool accept = false;
    ImGui::BeginChild("Assignments", ImVec2(0, 290), ImGuiChildFlags_Borders);
    for (const auto& entry : hhkbs::keymap::ScanCodeCatalog::entries()) {
        char hex[12];
        std::snprintf(hex, sizeof(hex), "0x%04X", entry.code);
        const auto label = entry.category + " / " + entry.label + "  " + hex;
        if (!needle.empty() && lower(label).find(needle) == std::string::npos) continue;
        ImGui::PushID(entry.code);
        if (ImGui::Selectable(label.c_str(), lower(raw_.data()) == lower(hex), ImGuiSelectableFlags_AllowDoubleClick)) {
            std::snprintf(raw_.data(), raw_.size(), "%s", hex);
            accept = ImGui::IsMouseDoubleClicked(0);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::InputText("Raw hex code", raw_.data(), raw_.size());
    std::string_view value(raw_.data());
    if (value.starts_with("0x") || value.starts_with("0X")) value.remove_prefix(2);
    unsigned code = 0;
    const auto result = std::from_chars(value.data(), value.data()+value.size(), code, 16);
    const bool valid = !value.empty() && value.size() <= 4 && result.ec == std::errc{} &&
                       result.ptr == value.data()+value.size() && code <= 0xFFFF;
    if (!valid) ImGui::TextUnformatted("Enter a hexadecimal value from 0000 to FFFF.");
    ImGui::BeginDisabled(!valid);
    accept |= ImGui::Button("Assign", ImVec2(110, 0));
    ImGui::EndDisabled();
    if (accept && valid) return static_cast<hhkbs::keymap::Keymap::ScanCode>(code);
    return std::nullopt;
}
