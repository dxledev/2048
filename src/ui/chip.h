#pragma once

#include <QColor>
#include <QPushButton>

namespace g2048 {

/**
 * @brief Checkable choice button whose outline is stroked on whole device pixels.
 *
 * Style sheet borders fall on fractional device pixels under fractional display
 * scaling, so one side of a highlighted chip could render thinner than the
 * others. The style sheet keeps the border transparent and the chip strokes it.
 */
class Chip : public QPushButton {
    Q_OBJECT
public:
    /** @param radius Corner radius in logical pixels, matching the style sheet. */
    explicit Chip(const QString &text, qreal radius, QWidget *parent = nullptr);
    /** @brief Outline colors when unchecked and when checked. */
    void setEdges(const QColor &normal, const QColor &checked);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal radius_;
    QColor normal_, checked_;
};

}
