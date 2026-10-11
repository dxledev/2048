#pragma once

#include "core/theme.h"
#include <QFrame>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

namespace g2048 {

/**
 * @brief In-window modal: a blurred snapshot of the game, a tint, and a centered card.
 *
 * The modal is a child widget covering its parent, never a separate window. While
 * open it owns keyboard focus; Escape and clicks outside the card dismiss it when
 * the modal is dismissable.
 */
class Modal : public QWidget {
    Q_OBJECT
public:
    /** @brief Create a hidden modal over @p parent that blurs @p background. */
    Modal(QWidget *parent, QWidget *background, const QString &name);
    /** @brief Card frame hosting the modal's content. */
    QFrame *card() const { return card_; }
    /** @brief Vertical layout inside the card. */
    QVBoxLayout *body() const { return body_; }
    /** @brief Allow Escape and outside clicks to close the modal. */
    void setDismissable(bool dismissable) { dismissable_ = dismissable; }
    /** @brief Update tint and translucency, recapturing the background when open. */
    void setTheme(const Theme &theme);
    /** @brief Capture the background, cover the parent, and take focus. */
    void open();

public slots:
    /** @brief Hide the modal, release its snapshot, and emit dismissed(). */
    void dismiss();

signals:
    /** @brief Emitted whenever the modal closes. */
    void dismissed();
    /** @brief Emitted for Return/Enter so the owner can trigger the default action. */
    void accepted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *background_;
    QFrame *card_;
    QVBoxLayout *body_;
    QPixmap snapshot_;
    QColor base_, tint_;
    bool dismissable_ = true;
    bool capturing_ = false;
    /** @brief Render, downscale, and blur the background once per open or resize. */
    void capture();
};

}
