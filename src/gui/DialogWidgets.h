#pragma once
#include <initializer_list>
#include <string>

// Shared pieces so every dialog has the same title, error line and footer.
namespace dialog {

struct FooterButton {
    const char* label;
    bool primary = false;  // the one accented action of the dialog
    bool danger = false;   // destructive action, drawn in red
    bool enabled = true;
};

void title(const char* text);
void hint(const char* text);  // dimmed explanatory line under the title
void error(const std::string& message);

// Right-aligned button row. Returns the index of the pressed button, or -1.
// Order them so the accented action comes last, with Cancel or Back before it.
int footer(std::initializer_list<FooterButton> buttons);

}
