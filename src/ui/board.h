#pragma once

#include "core/game.h"
#include "core/theme.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <algorithm>

namespace g2048 {

class WonOverlay;

/** @brief Square game board that paints tiles and animates slides, merges, and spawns. */
class Board : public QWidget {
    Q_OBJECT
public:
    /** @brief What a click picks: nothing (swipes move), a tile to swap, or a value to remove. */
    enum class Pick { None, Swap, Remove };

    /** @brief Create the board and its hidden win overlay. */
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
    /** @brief Corner radius of a board drawn @p side pixels wide. */
    static double cornerRadius(double side) { return std::min(16.0, side * .03); }
    /** @brief Show the win overlay while the game's win is pending and no animation runs; hide it otherwise. */
    void refreshWin();
    /** @brief Enter or leave pick mode; clicks emit picked() and swipes are ignored while picking. */
    void setPick(Pick pick);
    Pick pick() const { return pick_; }
    /** @brief Ring the tile in cell @p index as the first pick of a swap, or none for -1. */
    void setSelected(int index);
    /** @brief Return the rectangle of cell @p index in widget coordinates. */
    QRectF cellBounds(int index) const { return cellRect(index, boardRect()); }
    /** @brief Return the cell under @p position, or -1 between and outside cells. */
    int cellAt(const QPointF &position) const;
    /** @brief Return whether the win overlay is fully revealed. */
    bool showingWin() const;
    QSize sizeHint() const override { return {420, 420}; }
    QSize minimumSizeHint() const override { return {220, 220}; }

signals:
    /** @brief Emitted for a mouse or touch swipe across the board. */
    void swiped(Direction direction);
    /** @brief Emitted when a cell is clicked in pick mode. */
    void picked(int index);
    /** @brief Emitted when the win overlay finishes its reveal. */
    void winRevealed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    const Game *game_ = nullptr;
    Theme theme_;
    Turn turn_;
    QTimer timer_;
    QElapsedTimer clock_;
    QPointF pressed_;
    bool pressing_ = false;
    Pick pick_ = Pick::None;
    int selected_ = -1, hovered_ = -1, pressedCell_ = -1;
    WonOverlay *wonOverlay_;
    /** @brief Top-left corner and edge length of a cell on a board drawn in @p board. */
    QRectF cellRect(int index, const QRectF &board) const;
    /** @brief Draw the board, its cells, and the current animation frame into @p board. */
    void paintBoard(QPainter &painter, const QRectF &board) const;
    /** @brief Paint the settled board into a small image for the win overlay to blur. */
    QPixmap snapshot() const;
    /** @brief Point at tiles that a click would pick, and use the normal cursor elsewhere. */
    void updateCursor();
    /** @brief Ring the selected tile and whatever the pointer would pick. */
    void paintPicks(QPainter &painter, const QRectF &board) const;
    /** @brief Draw one tile face and its number, scaled around its center. */
    void paintTile(QPainter &painter, const QRectF &cell, int value, double scale) const;
};

}
