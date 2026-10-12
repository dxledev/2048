/**
 * @file won_overlay.cpp
 * @brief Animated win treatment above a captured, blurred 2048 board.
 */
#include "won_overlay.h"
#include "board.h"

#include <QFontMetricsF>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace g2048 {

WonOverlay::WonOverlay(QWidget *parent)
    : QWidget(parent), animation_(this, "progress") {
    setObjectName("wonOverlay");
    setAccessibleName("You won");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    animation_.setDuration(650);
    animation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&animation_, &QPropertyAnimation::finished, this, &WonOverlay::revealed);
    hide();
}

void WonOverlay::setSnapshot(const QPixmap &board, const Theme &theme) {
    theme_ = theme;
    QGraphicsScene scene;
    auto *item = scene.addPixmap(board);
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(8);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(blur);
    blurredBoard_ = QPixmap(board.size());
    blurredBoard_.fill(theme.color("board"));
    QPainter painter(&blurredBoard_);
    scene.render(&painter, QRectF(blurredBoard_.rect()), QRectF(board.rect()));
    update();
}

void WonOverlay::reveal(const QPixmap &board, const Theme &theme) {
    animation_.stop();
    setSnapshot(board, theme);
    setProgress(0);
    show();
    raise();
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.start();
}

void WonOverlay::clear() {
    animation_.stop();
    hide();
    blurredBoard_ = {};
    progress_ = 0;
}

void WonOverlay::setProgress(qreal progress) {
    progress_ = progress;
    update();
}

void WonOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const double radius = Board::cornerRadius(width());
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setClipPath(clip);
    painter.setOpacity(progress_);
    painter.drawPixmap(rect(), blurredBoard_);
    auto veil = theme_.color("surface");
    veil.setAlphaF(.4);
    painter.fillRect(rect(), veil);

    const QString text = "You won!";
    auto wonFont = font();
    wonFont.setPixelSize(std::max(1, qRound(width() * .16)));
    wonFont.setWeight(QFont::Bold);
    // Shrink to keep the message inside the board with a margin on each side.
    const double advance = QFontMetricsF(wonFont).horizontalAdvance(text);
    if (advance > width() * .84)
        wonFont.setPixelSize(std::max(1, int(wonFont.pixelSize() * width() * .84 / advance)));
    const auto center = QRectF(rect()).center();
    painter.translate(center.x(), center.y() + (1 - progress_) * 12);
    const qreal scale = .92 + progress_ * .08;
    painter.scale(scale, scale);
    painter.setFont(wonFont);
    painter.setPen(theme_.color("accent"));
    painter.drawText(QRectF(-width() / 2.0, -height() / 2.0, width(), height()), Qt::AlignCenter, text);
}

}
