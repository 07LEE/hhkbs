#pragma once

#include "keymap/Keymap.h"

#include <QRectF>
#include <QWidget>

#include <cstddef>
#include <optional>

namespace hhkbs::keymap {
struct KeyPosition;
}

class KeyboardWidget final : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardWidget(QWidget* parent = nullptr);

    void setKeymap(const hhkbs::keymap::Keymap* keymap);
    void setLayer(std::size_t layer);
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void keyActivated(std::size_t slot);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] QRectF keyRect(const hhkbs::keymap::KeyPosition& key) const;
    [[nodiscard]] std::optional<std::size_t> slotAt(const QPointF& point) const;

    const hhkbs::keymap::Keymap* keymap_{};
    std::size_t layer_{};
    std::optional<std::size_t> hoveredSlot_;
};
