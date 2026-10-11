#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <array>

namespace g2048 {

/** @brief Number of tile color steps: 2, 4, ..., 2048, then one shared step for larger tiles. */
inline constexpr int tileSteps = 12;

/** @brief Validated palette, source binding, and cached tile colors. */
struct Theme {
    QString name; ///< Display name or preset identifier.
    QMap<QString, QColor> colors; ///< Every required key plus any tile overrides.
    QString sourceKind; ///< External source type, empty when following no source.
    QString sourcePath; ///< Link, directory, or file used by the external source.
    std::array<QColor, tileSteps> tiles{}; ///< Cached tile faces, overrides applied.
    std::array<QColor, tileSteps> tileText{}; ///< Cached readable digit colors per tile.
    /** @brief Read a configured color by key. */
    QColor color(const QString &key) const { return colors.value(key); }
    /** @brief Return a configured color as #RRGGBB, or #AARRGGBB when translucent. */
    QString hex(const QString &key) const;
    /** @brief Tile step for a face value: log2(value) - 1, clamped to the shared final step. */
    static int step(int value);
    /** @brief Serialize the palette and source metadata. */
    QJsonObject toJson() const;
    bool operator==(const Theme &) const = default;
};

/** @brief Required palette keys in canonical order. */
QStringList colorKeys();
/** @brief Optional per-tile override keys: tile_2 ... tile_2048, tile_super. */
QStringList tileKeys();
/** @brief Keys whose alpha channel is preserved; every other key is stored opaque. */
bool keepsAlpha(const QString &key);
/** @brief Names of the built-in palettes. */
QStringList presetNames();
/** @brief Construct a built-in palette by name. */
Theme presetTheme(const QString &name);
/** @brief Name of the palette used before any theme is chosen. */
QString defaultPresetName();
/** @brief Parse and validate a complete theme object. */
Theme parseTheme(const QJsonObject &data);
/** @brief Load and validate a theme JSON file. */
Theme loadTheme(const QString &path);
/** @brief Apply KEY=COLOR assignments (or KEY= to clear a tile override) to a copy of a theme. */
Theme setColors(Theme theme, const QStringList &assignments);

}
