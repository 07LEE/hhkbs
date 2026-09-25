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

// The optional note sits dimmed on the title's baseline, to its right.
void title(const char* text, const char* note = nullptr);
void hint(const char* text);  // dimmed explanatory line under the title
void error(const std::string& message);

// For a dialog with a fixed height: moves down so the footer that follows ends at the bottom edge.
void pinFooter();

// Right-aligned button row. Returns the index of the pressed button, or -1.
// Order them so the accented action comes last, with Cancel or Back before it.
int footer(std::initializer_list<FooterButton> buttons);

// The same with two groups: `left` starts at the left edge, `right` ends at the right edge. The returned index counts
// through `left` first and then `right`.
int footer(std::initializer_list<FooterButton> left, std::initializer_list<FooterButton> right);

}
