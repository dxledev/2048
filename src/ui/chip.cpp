/**
 * @file chip.cpp
 * @brief Choice button with a pixel-aligned outline.
 */
#include "chip.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace g2048 {

Chip::Chip(const QString &text, qreal radius, QWidget *parent) : QPushButton(text, parent), radius_(radius) {}

void Chip::setEdges(const QColor &normal, const QColor &checked) {
    normal_ = normal;
    checked_ = checked;
    update();
}

void Chip::paintEvent(QPaintEvent *event) {
    QPushButton::paintEvent(event);
    // Snap the outline inward to the device pixels this widget fully covers, so every side is equally thick.
    const qreal ratio = devicePixelRatioF();
    const QPointF origin = mapTo(window(), QPoint());
    const qreal left = std::ceil(origin.x() * ratio) / ratio - origin.x();
    const qreal top = std::ceil(origin.y() * ratio) / ratio - origin.y();
    const qreal right = std::floor((origin.x() + width()) * ratio) / ratio - origin.x();
    const qreal bottom = std::floor((origin.y() + height()) * ratio) / ratio - origin.y();
    const qreal pen = std::max<qreal>(1, std::round(ratio)) / ratio;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(isChecked() ? checked_ : normal_, pen));
    painter.setBrush(Qt::NoBrush);
    const qreal inset = pen / 2;
    painter.drawRoundedRect(QRectF(QPointF(left, top), QPointF(right, bottom)).adjusted(inset, inset, -inset, -inset),
                            radius_ - inset, radius_ - inset);
}

}
