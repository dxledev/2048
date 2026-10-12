/**
 * @file power_bar.cpp
 * @brief Tray of power-up buttons painted from theme colors.
 *
 * Each button is a rounded square with its glyph centered high and one pill
 * per charge along the bottom edge; with unlimited uses there are no pills and
 * the glyph sits in the middle. Everything scales with the button edge, so
 * the window can resize the tray with the board, and the tray turns from a row
 * into a column when it moves beside the board. A checked button is the armed
 * power-up and inverts to the accent color.
 */
#include "power_bar.h"
#include "core/color.h"

#include <QEnterEvent>
#include <QBoxLayout>
#include <QPainter>
#include <cmath>
#include <algorithm>

namespace g2048 {
namespace {

constexpr double trayPadding = .14;
constexpr double traySpacing = .14;

}

PowerButton::PowerButton(Glyph glyph, QWidget *parent) : QAbstractButton(parent), glyph_(glyph) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void PowerButton::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

void PowerButton::setCharges(int count, int capacity) {
    charges_ = count;
    capacity_ = capacity;
    update();
}

void PowerButton::setUnlimited(bool unlimited) {
    if (unlimited == unlimited_)
        return;
    unlimited_ = unlimited;
    update();
}

void PowerButton::enterEvent(QEnterEvent *event) {
    QAbstractButton::enterEvent(event);
    update();
}

void PowerButton::leaveEvent(QEvent *event) {
    QAbstractButton::leaveEvent(event);
    update();
}

void PowerButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto surface = opaque(theme_.color("surface_alt")), border = opaque(theme_.color("border"));
    const auto text = opaque(theme_.color("text")), accent = opaque(theme_.color("accent"));
    const auto accentText = opaque(theme_.color("accent_text"));
    const bool armed = isChecked(), enabled = isEnabled();
    QColor fill = surface, edge = border, ink = text;
    if (armed) {
        fill = accent;
        edge = accent;
        ink = accentText;
    } else if (!enabled) {
        ink = mix(surface, text, .38);
    } else if (isDown()) {
        fill = mix(surface, accent, .26);
    } else if (underMouse()) {
        fill = mix(surface, accent, .14);
        edge = mix(border, accent, .5);
    }
    const double side = std::min(width(), height()), radius = side * .26;
    const QRectF face = QRectF(QPointF((width() - side) / 2, (height() - side) / 2), QSizeF(side, side))
                            .adjusted(.5, .5, -.5, -.5);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(face, radius, radius);

    const double glyphSize = side * .5;
    const double glyphTop = unlimited_ ? face.center().y() - glyphSize / 2 : face.top() + side * .11;
    paintGlyph(painter, glyph_, QRectF(face.center().x() - glyphSize / 2, glyphTop, glyphSize, glyphSize), ink);
    if (unlimited_)
        return;

    // One pill per use: remaining ones in the ink color, spent ones faint.
    const double pipWidth = side * .22, pipHeight = std::max(3.0, side * .065), gap = side * .06;
    const double rowWidth = capacity_ * pipWidth + (capacity_ - 1) * gap;
    const QColor spent = mix(fill, ink, enabled || armed ? .24 : .16);
    painter.setPen(Qt::NoPen);
    for (int index = 0; index < capacity_; ++index) {
        painter.setBrush(index < charges_ ? ink : spent);
        painter.drawRoundedRect(QRectF(face.center().x() - rowWidth / 2 + index * (pipWidth + gap),
                                       face.bottom() - side * .12 - pipHeight, pipWidth, pipHeight),
                                pipHeight / 2, pipHeight / 2);
    }
}

PowerBar::PowerBar(QWidget *parent) : QWidget(parent) {
    setObjectName("powerBar");
    setAccessibleName("Power-ups");
    auto *row = new QBoxLayout(QBoxLayout::LeftToRight, this);
    const std::array glyphs{Glyph::Undo, Glyph::Swap, Glyph::Remove};
    for (int index = 0; index < Game::powerCount; ++index) {
        buttons_[index] = new PowerButton(glyphs[index]);
        row->addWidget(buttons_[index]);
        connect(buttons_[index], &QAbstractButton::clicked, this, [this, index] { emit activated(static_cast<Power>(index)); });
    }
    setUnit(64);
}

void PowerBar::setTheme(const Theme &theme) {
    theme_ = theme;
    for (auto *item : buttons_)
        item->setTheme(theme);
    update();
}

void PowerBar::setUnit(int unit) {
    unit_ = unit;
    const int padding = std::lround(unit * trayPadding);
    layout()->setContentsMargins(padding, padding, padding, padding);
    layout()->setSpacing(std::lround(unit * traySpacing));
    for (auto *item : buttons_)
        item->setFixedSize(unit, unit);
    const int length = Game::powerCount * unit + (Game::powerCount - 1) * std::lround(unit * traySpacing) + 2 * padding;
    setFixedSize(orientation_ == Qt::Horizontal ? QSize(length, thicknessFor(unit)) : QSize(thicknessFor(unit), length));
    updateGeometry();
}

void PowerBar::setOrientation(Qt::Orientation orientation) {
    if (orientation == orientation_)
        return;
    orientation_ = orientation;
    static_cast<QBoxLayout *>(layout())->setDirection(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight
                                                                                    : QBoxLayout::TopToBottom);
    setUnit(unit_);
}

int PowerBar::thicknessFor(int unit) {
    return unit + 2 * std::lround(unit * trayPadding);
}

void PowerBar::setArmed(const std::optional<Power> &power) {
    for (int index = 0; index < Game::powerCount; ++index)
        buttons_[index]->setChecked(power && static_cast<int>(*power) == index);
}

void PowerBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(opaque(theme_.color("border")), 1));
    painter.setBrush(opaque(theme_.color("surface")));
    const double radius = unit_ * .26 + unit_ * trayPadding;
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), radius, radius);
}

}
