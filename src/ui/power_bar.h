#pragma once

#include "core/game.h"
#include "core/theme.h"
#include "icons.h"
#include <QAbstractButton>
#include <QWidget>
#include <array>
#include <optional>

namespace g2048 {

/** @brief Square tray button: a glyph over a row of pips, one per remaining use. */
class PowerButton : public QAbstractButton {
    Q_OBJECT
public:
    /** @brief Create a button that draws @p glyph. */
    explicit PowerButton(Glyph glyph, QWidget *parent = nullptr);
    /** @brief Repaint with a new palette. */
    void setTheme(const Theme &theme);
    /** @brief Show @p count filled pips out of @p capacity. */
    void setCharges(int count, int capacity);
    int charges() const { return charges_; }
    QSize sizeHint() const override { return {64, 64}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    /** @brief Armed state is owned by the window, so a click never toggles it. */
    void nextCheckState() override {}

private:
    Glyph glyph_;
    Theme theme_;
    int charges_ = 0, capacity_ = Game::maximumCharges;
};

/** @brief Rounded tray beside or under the board holding the undo, swap, and delete power-up buttons. */
class PowerBar : public QWidget {
    Q_OBJECT
public:
    explicit PowerBar(QWidget *parent = nullptr);
    /** @brief Button of @p power. */
    PowerButton *button(Power power) const { return buttons_[static_cast<int>(power)]; }
    /** @brief Repaint with a new palette. */
    void setTheme(const Theme &theme);
    /** @brief Set the button edge in pixels; padding and spacing follow. */
    void setUnit(int unit);
    int unit() const { return unit_; }
    /** @brief Lay the buttons out in a row (under the board) or a column (beside it). */
    void setOrientation(Qt::Orientation orientation);
    Qt::Orientation orientation() const { return orientation_; }
    /** @brief Tray thickness (height of a row, width of a column) for buttons @p unit pixels wide. */
    static int thicknessFor(int unit);
    /** @brief Check the button of @p power, or none, and uncheck the others. */
    void setArmed(const std::optional<Power> &power);

signals:
    /** @brief Emitted when the player clicks the button of a power. */
    void activated(Power power);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::array<PowerButton *, Game::powerCount> buttons_{};
    Theme theme_;
    int unit_ = 0;
    Qt::Orientation orientation_ = Qt::Horizontal;
};

}
