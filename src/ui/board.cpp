/**
 * @file board.cpp
 * @brief Board painting, tile animation, and swipe input.
 *
 * An animation has two phases: tiles slide from their previous cells, then
 * merged tiles pulse and the new tile grows in. A new move finishes any running
 * animation first, so input is never queued or dropped. Frames are painted
 * directly from game state; no per-tile widgets or cached images are kept.
 * While a win is pending, a WonOverlay covers the board with a blurred copy.
 */
#include "board.h"
#include "core/color.h"
#include "won_overlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace g2048 {
namespace {

constexpr int slideMilliseconds = 105;
constexpr int popMilliseconds = 115;

double easeOut(double t) {
    return 1 - std::pow(1 - std::clamp(t, 0.0, 1.0), 3);
}

}

Board::Board(QWidget *parent) : QWidget(parent), wonOverlay_(new WonOverlay(this)) {
    setObjectName("board");
    setAccessibleName("Game board");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    timer_.setInterval(8);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (clock_.elapsed() >= slideMilliseconds + popMilliseconds)
            finish();
        update();
    });
    connect(wonOverlay_, &WonOverlay::revealed, this, &Board::winRevealed);
}

void Board::setGame(const Game *game) {
    wonOverlay_->clear();
    setAccessibleDescription({});
    game_ = game;
    finish();
    update();
}

void Board::setTheme(const Theme &theme) {
    theme_ = theme;
    if (!wonOverlay_->isHidden())
        wonOverlay_->setSnapshot(snapshot(), theme_);
    update();
}

void Board::animate() {
    if (!game_)
        return;
    turn_ = game_->lastTurn();
    clock_.start();
    timer_.start();
    update();
}

void Board::finish() {
    timer_.stop();
    turn_ = {};
    update();
    refreshWin();
}

void Board::refreshWin() {
    const bool won = game_ && game_->winPending();
    if (won && wonOverlay_->isHidden() && !animating()) {
        wonOverlay_->setGeometry(boardRect().toRect());
        wonOverlay_->reveal(snapshot(), theme_);
        setAccessibleDescription("You won");
    } else if (!won && !wonOverlay_->isHidden()) {
        wonOverlay_->clear();
        setAccessibleDescription({});
    }
}

bool Board::showingWin() const {
    return !wonOverlay_->isHidden() && wonOverlay_->progress() >= 1;
}

QPixmap Board::snapshot() const {
    // Bound the cached blur independently of window size and display scale.
    QPixmap image(384, 384);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font());
    paintBoard(painter, QRectF(image.rect()));
    return image;
}

QRectF Board::boardRect() const {
    const double side = std::max(0, std::min(width(), height()));
    return {(width() - side) / 2, (height() - side) / 2, side, side};
}

QRectF Board::cellRect(int index, const QRectF &board) const {
    const int size = game_->size();
    const double gap = std::max(5.0, board.width() * (size > 5 ? .022 : .028));
    const double cell = (board.width() - gap * (size + 1)) / size;
    return {board.left() + gap + index % size * (cell + gap), board.top() + gap + index / size * (cell + gap), cell, cell};
}

void Board::paintTile(QPainter &painter, const QRectF &cell, int value, double scale) const {
    const int step = Theme::step(value);
    const QPointF center = cell.center();
    const QRectF face(center.x() - cell.width() * scale / 2, center.y() - cell.height() * scale / 2,
                      cell.width() * scale, cell.height() * scale);
    const double radius = std::max(4.0, face.width() * .1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme_.tiles[step]);
    painter.drawRoundedRect(face, radius, radius);
    const QString label = QString::number(value);
    QFont font = painter.font();
    const std::array ratios{.48, .48, .42, .34, .28, .23, .2};
    font.setPixelSize(std::max(8, int(face.height() * ratios[std::min<qsizetype>(label.size(), 6)])));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(theme_.tileText[step]);
    painter.drawText(face, Qt::AlignCenter, label);
}

void Board::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintBoard(painter, boardRect());
}

void Board::paintBoard(QPainter &painter, const QRectF &board) const {
    const double radius = cornerRadius(board.width());
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme_.color("board"));
    painter.drawRoundedRect(board, radius, radius);
    if (!game_)
        return;
    const int cells = game_->size() * game_->size();
    painter.setBrush(theme_.color("cell"));
    for (int index = 0; index < cells; ++index) {
        const auto cell = cellRect(index, board);
        const double cellRadius = std::max(4.0, cell.width() * .1);
        painter.drawRoundedRect(cell, cellRadius, cellRadius);
    }
    const qint64 elapsed = timer_.isActive() ? clock_.elapsed() : slideMilliseconds + popMilliseconds;
    if (elapsed < slideMilliseconds && !turn_.motions.empty()) {
        const double t = easeOut(double(elapsed) / slideMilliseconds);
        for (const auto &motion : turn_.motions) {
            const auto from = cellRect(motion.from, board), to = cellRect(motion.to, board);
            const QRectF cell(from.topLeft() + (to.topLeft() - from.topLeft()) * t, from.size());
            paintTile(painter, cell, motion.value, 1);
        }
        return;
    }
    const double pop = std::clamp(double(elapsed - slideMilliseconds) / popMilliseconds, 0.0, 1.0);
    for (int index = 0; index < cells; ++index) {
        const auto &tile = game_->at(index);
        if (!tile.value)
            continue;
        double scale = 1;
        if (timer_.isActive() && index == turn_.spawned)
            scale = .35 + .65 * easeOut(pop);
        else if (timer_.isActive() && std::ranges::find(turn_.merged, index) != turn_.merged.end())
            scale = 1 + .12 * std::sin(pop * std::numbers::pi);
        paintTile(painter, cellRect(index, board), tile.value, scale);
    }
}

void Board::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // The snapshot has a fixed size, so a resize only moves the overlay.
    wonOverlay_->setGeometry(boardRect().toRect());
}

void Board::mousePressEvent(QMouseEvent *event) {
    pressed_ = event->position();
    pressing_ = event->button() == Qt::LeftButton;
    QWidget::mousePressEvent(event);
}

void Board::mouseReleaseEvent(QMouseEvent *event) {
    if (!pressing_ || event->button() != Qt::LeftButton)
        return;
    pressing_ = false;
    const auto delta = event->position() - pressed_;
    const double threshold = std::max(24.0, boardRect().width() * .06);
    if (std::max(std::abs(delta.x()), std::abs(delta.y())) < threshold)
        return;
    if (std::abs(delta.x()) > std::abs(delta.y()))
        emit swiped(delta.x() > 0 ? Direction::Right : Direction::Left);
    else
        emit swiped(delta.y() > 0 ? Direction::Down : Direction::Up);
}

}
