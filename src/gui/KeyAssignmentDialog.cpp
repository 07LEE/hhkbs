#include "gui/KeyAssignmentDialog.h"

#include "keymap/ScanCodeCatalog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

KeyAssignmentDialog::KeyAssignmentDialog(
    const hhkbs::keymap::Keymap::ScanCode currentCode,
    QWidget* parent)
    : QDialog(parent)
    , selectedCode_(currentCode)
{
    setWindowTitle(QStringLiteral("Assign input"));
    setMinimumSize(460, 540);

    auto* layout = new QVBoxLayout(this);
    auto* instruction = new QLabel(
        QStringLiteral("Choose a key or device function, or enter a raw 16-bit scan code."));
    instruction->setWordWrap(true);
    layout->addWidget(instruction);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Search keys and categories…"));
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);

    list_ = new QListWidget;
    list_->setAlternatingRowColors(true);
    layout->addWidget(list_, 1);

    auto* customLabel = new QLabel(QStringLiteral("Raw scan code"));
    layout->addWidget(customLabel);
    customCodeEdit_ = new QLineEdit(
        QStringLiteral("0x%1").arg(currentCode, 4, 16, QLatin1Char('0')).toUpper());
    customCodeEdit_->setValidator(
        new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("^(0[xX])?[0-9A-Fa-f]{1,4}$")),
            customCodeEdit_));
    layout->addWidget(customCodeEdit_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    populate();

    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString& query) {
        filterItems(query);
    });
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        const auto code = item->data(Qt::UserRole).toUInt();
        customCodeEdit_->setText(
            QStringLiteral("0x%1").arg(code, 4, 16, QLatin1Char('0')).toUpper());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        acceptSelection();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        acceptSelection();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

hhkbs::keymap::Keymap::ScanCode KeyAssignmentDialog::selectedScanCode() const noexcept
{
    return selectedCode_;
}

void KeyAssignmentDialog::populate()
{
    for (const auto& entry : hhkbs::keymap::ScanCodeCatalog::entries()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2")
                .arg(
                    QString::fromStdString(entry.category),
                    QString::fromStdString(entry.label)),
            list_);
        item->setData(Qt::UserRole, entry.code);
        item->setToolTip(
            QStringLiteral("0x%1").arg(entry.code, 4, 16, QLatin1Char('0')).toUpper());
        if (entry.code == selectedCode_) {
            list_->setCurrentItem(item);
            list_->scrollToItem(item);
        }
    }
}

void KeyAssignmentDialog::filterItems(const QString& query)
{
    const auto needle = query.trimmed();
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        item->setHidden(
            !needle.isEmpty()
            && !item->text().contains(needle, Qt::CaseInsensitive)
            && !item->toolTip().contains(needle, Qt::CaseInsensitive));
    }
}

void KeyAssignmentDialog::acceptSelection()
{
    auto value = customCodeEdit_->text().trimmed();
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        value.remove(0, 2);
    }

    bool ok = false;
    const auto code = value.toUShort(&ok, 16);
    if (!ok) {
        QMessageBox::warning(
            this,
            QStringLiteral("Invalid scan code"),
            QStringLiteral("Enter a hexadecimal value between 0x0000 and 0xFFFF."));
        return;
    }

    selectedCode_ = code;
    accept();
}
