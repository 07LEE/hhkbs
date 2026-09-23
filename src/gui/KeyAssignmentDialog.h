#pragma once

#include "keymap/Keymap.h"

#include <QDialog>

class QLineEdit;
class QListWidget;

class KeyAssignmentDialog final : public QDialog {
public:
    explicit KeyAssignmentDialog(
        hhkbs::keymap::Keymap::ScanCode currentCode,
        QWidget* parent = nullptr);

    [[nodiscard]] hhkbs::keymap::Keymap::ScanCode selectedScanCode() const noexcept;

private:
    void populate();
    void filterItems(const QString& query);
    void acceptSelection();

    QLineEdit* searchEdit_{};
    QLineEdit* customCodeEdit_{};
    QListWidget* list_{};
    hhkbs::keymap::Keymap::ScanCode selectedCode_{};
};
