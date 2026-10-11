/**
 * @file icons.cpp
 * @brief Theme-colored vector glyphs and the brand mark.
 *
 * Nothing here ships as a bitmap or a fixed-color image: each icon is a
 * painter path stroked or filled with a palette color at the screen's device
 * pixel ratio, and regenerated whenever the theme changes.
 */
#include "icons.h"
#include "core/color.h"

#include <QPainterPath>
#include <cmath>
#include <numbers>

namespace g2048 {
namespace {

/** Arc on a circle with an arrowhead at its end, following the direction of travel. */
void arrowArc(QPainterPath &path, const QRectF &circle, double start, double sweep) {
    path.arcMoveTo(circle, start);
    path.arcTo(circle, start, sweep);
    const double end = (start + sweep) * std::numbers::pi / 180;
    const double radius = circle.width() / 2;
    const QPointF tip(circle.center().x() + radius * std::cos(end), circle.center().y() - radius * std::sin(end));
    const double sign = sweep < 0 ? 1 : -1;
    const QPointF direction(sign * std::sin(end), sign * std::cos(end));
    const QPointF normal(-direction.y(), direction.x());
    path.moveTo(tip - direction * 3.6 + normal * 3.6);
    path.lineTo(tip);
    path.lineTo(tip - direction * 3.6 - normal * 3.6);
}

}

void paintGlyph(QPainter &painter, Glyph glyph, const QRectF &bounds, const QColor &color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(bounds.topLeft());
    painter.scale(bounds.width() / 24, bounds.height() / 24);
    QPen pen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (glyph) {
    case Glyph::Undo:
        path.moveTo(8.5, 4.5);
        path.lineTo(4, 9);
        path.lineTo(8.5, 13.5);
        path.moveTo(4, 9);
        path.lineTo(14.5, 9);
        path.arcTo(QRectF(10, 9, 10, 10), 90, -180);
        path.lineTo(9, 19);
        break;
    case Glyph::Restart:
        arrowArc(path, QRectF(4.5, 4.5, 15, 15), 110, -300);
        break;
    case Glyph::Trophy:
        path.moveTo(7.5, 4.5);
        path.lineTo(16.5, 4.5);
        path.lineTo(16.5, 9);
        path.arcTo(QRectF(7.5, 4.5, 9, 9), 0, -180);
        path.closeSubpath();
        path.moveTo(7.5, 6.5);
        path.arcTo(QRectF(3.5, 6.5, 4, 4), 90, 180);
        path.moveTo(16.5, 6.5);
        path.arcTo(QRectF(16.5, 6.5, 4, 4), 90, -180);
        path.moveTo(12, 13.5);
        path.lineTo(12, 18);
        path.moveTo(8, 19.5);
        path.lineTo(16, 19.5);
        break;
    case Glyph::Grid:
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column)
                path.addRoundedRect(QRectF(4 + column * 9, 4 + row * 9, 7, 7), 1.8, 1.8);
        }
        break;
    case Glyph::Chart:
        path.moveTo(4, 19.5);
        path.lineTo(20, 19.5);
        path.moveTo(7.5, 15.5);
        path.lineTo(7.5, 11.5);
        path.moveTo(12, 15.5);
        path.lineTo(12, 5);
        path.moveTo(16.5, 15.5);
        path.lineTo(16.5, 8.5);
        break;
    }
    painter.drawPath(path);
    painter.restore();
}

QIcon glyphIcon(Glyph glyph, const QColor &color, const QColor &disabled, qreal ratio, int size) {
    QIcon icon;
    for (const auto &[mode, tone] : {std::pair{QIcon::Normal, color}, std::pair{QIcon::Disabled, disabled}}) {
        QPixmap pixmap(QSize(size, size) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paintGlyph(painter, glyph, QRectF(0, 0, size, size), tone);
        painter.end();
        icon.addPixmap(pixmap, mode);
    }
    return icon;
}

QPixmap brandMark(const Theme &theme, int size, qreal ratio) {
    QPixmap result(QSize(size, size) * ratio);
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const double gap = size * .08, tile = (size - gap) / 2, radius = tile * .22;
    const std::array steps{1, 4, 7, 10};
    for (int index = 0; index < 4; ++index) {
        painter.setBrush(theme.tiles[steps[index]]);
        painter.drawRoundedRect(QRectF(index % 2 * (tile + gap), index / 2 * (tile + gap), tile, tile), radius, radius);
    }
    return result;
}

QIcon applicationIcon(const Theme &theme) {
    QIcon icon;
    for (const int size : {16, 32, 64, 128}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(opaque(theme.color("surface")));
        painter.drawRoundedRect(QRectF(0, 0, size, size), size * .22, size * .22);
        painter.drawPixmap(QRectF(size * .18, size * .18, size * .64, size * .64),
                           brandMark(theme, int(size * .64), 1), QRectF());
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

}
