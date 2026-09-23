#include "gui/KeyboardWidget.h"

#include "keymap/KeyboardLayout.h"
#include "keymap/ScanCodeCatalog.h"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace {

constexpr qreal layoutWidth = 15.0;
constexpr qreal layoutHeight = 6.1;
constexpr qreal outerMargin = 18.0;

}  // namespace

KeyboardWidget::KeyboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(340);
    setAccessibleName(QStringLiteral("HHKB Studio keymap"));
}

void KeyboardWidget::setKeymap(const hhkbs::keymap::Keymap* keymap)
{
    keymap_ = keymap;
    update();
}

void KeyboardWidget::setLayer(const std::size_t layer)
{
    if (layer >= hhkbs::keymap::Keymap::layerCount) {
        return;
    }
    layer_ = layer;
    hoveredSlot_.reset();
    update();
}

QSize KeyboardWidget::sizeHint() const
{
    return {1080, 440};
}

void KeyboardWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (keymap_ == nullptr) {
        return;
    }

    for (const auto& key : hhkbs::keymap::KeyboardLayout::usStudio()) {
        const auto rectangle = keyRect(key);
        const auto modified = keymap_->isKeyModified(layer_, key.slot);
        const auto hovered = hoveredSlot_ && *hoveredSlot_ == key.slot;

        QColor background = QColor(QStringLiteral("#f8f9fb"));
        QColor border = QColor(QStringLiteral("#cbd1da"));
        if (modified) {
            background = QColor(QStringLiteral("#e5efff"));
            border = QColor(QStringLiteral("#2b6de5"));
        } else if (hovered) {
            background = QColor(QStringLiteral("#eef2f8"));
            border = QColor(QStringLiteral("#8d98a8"));
        }

        painter.setPen(QPen(border, modified ? 2.0 : 1.0));
        painter.setBrush(background);
        painter.drawRoundedRect(rectangle, 7.0, 7.0);

        const auto unit = rectangle.height() / 0.82;
        QFont legendFont = font();
        legendFont.setPixelSize(std::max(8, static_cast<int>(unit * 0.14)));
        painter.setFont(legendFont);
        painter.setPen(QColor(QStringLiteral("#7a8492")));
        const QRectF legendRect = rectangle.adjusted(8, 5, -6, -4);
        painter.drawText(legendRect, Qt::AlignLeft | Qt::AlignTop, QString::fromStdString(key.legend));

        QFont assignmentFont = font();
        auto assignmentSize = std::max(9, static_cast<int>(unit * 0.18));
        assignmentFont.setPixelSize(assignmentSize);
        assignmentFont.setWeight(QFont::DemiBold);
        const auto assignmentLabel = QString::fromStdString(
            hhkbs::keymap::ScanCodeCatalog::labelFor(
                keymap_->scanCode(layer_, key.slot)));
        while (assignmentSize > 8
               && QFontMetrics(assignmentFont).horizontalAdvance(assignmentLabel)
                   > rectangle.width() - 10) {
            assignmentFont.setPixelSize(--assignmentSize);
        }
        painter.setFont(assignmentFont);
        painter.setPen(QColor(QStringLiteral("#20242a")));
        painter.drawText(
            rectangle.adjusted(5, 14, -5, -3),
            Qt::AlignCenter,
            assignmentLabel);

        if (modified) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#2b6de5")));
            painter.drawEllipse(
                QPointF(rectangle.right() - 7, rectangle.top() + 7),
                3,
                3);
        }
    }
}

void KeyboardWidget::mouseMoveEvent(QMouseEvent* event)
{
    const auto slot = slotAt(event->position());
    if (slot != hoveredSlot_) {
        hoveredSlot_ = slot;
        update();
    }
}

void KeyboardWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || keymap_ == nullptr) {
        return;
    }
    if (const auto slot = slotAt(event->position())) {
        emit keyActivated(*slot);
    }
}

void KeyboardWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    hoveredSlot_.reset();
    update();
}

QRectF KeyboardWidget::keyRect(const hhkbs::keymap::KeyPosition& key) const
{
    const auto availableWidth = std::max(1.0, width() - (outerMargin * 2));
    const auto availableHeight = std::max(1.0, height() - (outerMargin * 2));
    const auto unit =
        std::min(availableWidth / layoutWidth, availableHeight / layoutHeight);
    const auto originX = (width() - (layoutWidth * unit)) / 2.0;
    const auto originY = (height() - (layoutHeight * unit)) / 2.0;
    constexpr qreal gap = 3.0;

    return {
        originX + (key.x * unit) + gap,
        originY + (key.y * unit) + gap,
        (key.width * unit) - (gap * 2),
        (unit * 0.82) - gap,
    };
}

std::optional<std::size_t> KeyboardWidget::slotAt(const QPointF& point) const
{
    if (keymap_ == nullptr) {
        return std::nullopt;
    }
    for (const auto& key : hhkbs::keymap::KeyboardLayout::usStudio()) {
        if (keyRect(key).contains(point)) {
            return key.slot;
        }
    }
    return std::nullopt;
}
