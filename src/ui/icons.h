#pragma once

#include "core/theme.h"
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace g2048 {

/** @brief Line icons drawn with QPainter, so every glyph takes any theme color. */
enum class Glyph { Undo, Restart, Trophy, Grid, Chart };

/** @brief Stroke @p glyph into @p bounds, designed on a 24-unit grid. */
void paintGlyph(QPainter &painter, Glyph glyph, const QRectF &bounds, const QColor &color);
/** @brief Render @p glyph as an icon with normal and disabled colors at @p ratio. */
QIcon glyphIcon(Glyph glyph, const QColor &color, const QColor &disabled, qreal ratio, int size = 18);
/** @brief The 2048 mark: four rounded tiles in the theme's tile colors. */
QPixmap brandMark(const Theme &theme, int size, qreal ratio);
/** @brief Window icon generated from the current theme. */
QIcon applicationIcon(const Theme &theme);

}
