#pragma once

#include "core/theme.h"
#include <QPixmap>
#include <QPropertyAnimation>
#include <QWidget>

namespace g2048 {

/** @brief Animated win state drawn over a blurred board snapshot. */
class WonOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)
public:
    /** @brief Create a hidden, mouse-transparent overlay parented to the board. */
    explicit WonOverlay(QWidget *parent = nullptr);
    /** @brief Capture the won board and palette and animate the reveal. */
    void reveal(const QPixmap &board, const Theme &theme);
    /** @brief Replace the snapshot and palette without starting the animation. */
    void setSnapshot(const QPixmap &board, const Theme &theme);
    /** @brief Clear the captured image and hide the overlay. */
    void clear();
    /** @brief Animation progress in the range 0 to 1. */
    qreal progress() const { return progress_; }
    /** @brief Set animation progress and schedule a repaint. */
    void setProgress(qreal progress);

signals:
    /** @brief Emitted when the reveal animation has finished. */
    void revealed();

protected:
    /** @brief Paint the blurred board and the animated message. */
    void paintEvent(QPaintEvent *) override;

private:
    Theme theme_;
    QPixmap blurredBoard_;
    QPropertyAnimation animation_;
    qreal progress_ = 0;
};

}
