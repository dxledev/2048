#pragma once

#include "core/game.h"
#include "core/theme.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace g2048 {

/** @brief Square game board that paints tiles and animates slides, merges, and spawns. */
class Board : public QWidget {
    Q_OBJECT
public:
    explicit Board(QWidget *parent = nullptr);
    /** @brief Show @p game; the board keeps a pointer and never owns it. */
    void setGame(const Game *game);
    /** @brief Repaint with a new palette. */
    void setTheme(const Theme &theme);
    /** @brief Animate the game's last turn from the previous layout to the current one. */
    void animate();
    /** @brief Jump to the final frame of any running animation. */
    void finish();
    /** @brief Return whether an animation is running. */
    bool animating() const { return timer_.isActive(); }
    /** @brief Return the square in which the board is drawn. */
    QRectF boardRect() const;
    QSize sizeHint() const override { return {420, 420}; }
    QSize minimumSizeHint() const override { return {220, 220}; }

signals:
    /** @brief Emitted for a mouse or touch swipe across the board. */
    void swiped(Direction direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    const Game *game_ = nullptr;
    Theme theme_;
    Turn turn_;
    QTimer timer_;
    QElapsedTimer clock_;
    QPointF pressed_;
    bool pressing_ = false;
    /** @brief Top-left corner and edge length of a cell. */
    QRectF cellRect(int index) const;
    /** @brief Draw one tile face and its number, scaled around its center. */
    void paintTile(QPainter &painter, const QRectF &cell, int value, double scale) const;
};

}
