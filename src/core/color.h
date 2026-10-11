#pragma once

#include <QColor>
#include <QString>

namespace g2048 {

/**
 * @brief Parse a palette color, throwing std::invalid_argument when unsupported.
 *
 * Accepted forms: "#RRGGBB", "RRGGBB", "#AARRGGBB" / "AARRGGBB" (Qt order, alpha
 * first), "#RGB", and "Qt.rgba(r, g, b, a)" / "rgba(r, g, b, a)" whose channels are
 * numbers in [0, 1] or simple divisions such as "14 / 255". The alpha channel is
 * preserved in the returned color.
 */
QColor parseColor(const QString &text);
/** @brief Format as "#RRGGBB" when opaque, otherwise Qt's "#AARRGGBB". */
QString formatColor(const QColor &color);
/** @brief Drop alpha while keeping the exact 8-bit red, green, and blue channels. */
QColor opaque(const QColor &color);
/** @brief Linear interpolation of opaque RGB channels; alpha of the result is 255. */
QColor mix(const QColor &base, const QColor &tint, double amount);
/** @brief WCAG relative luminance of the RGB channels. */
double luminance(const QColor &color);
/** @brief WCAG contrast ratio between two colors' RGB channels. */
double contrast(const QColor &first, const QColor &second);
/** @brief Largest per-channel difference of the RGB channels, in 0-255 units. */
int distance(const QColor &first, const QColor &second);

}
