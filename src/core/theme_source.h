#pragma once

#include "theme.h"

namespace g2048 {

/** @brief Import a palette file or theme directory, optionally assigning a display name. */
Theme importPalette(const QString &path, const QString &name = {});
/** @brief Bind to an external source (active, static, or file) and resolve its current palette. */
Theme followTheme(const QString &kind, const QString &path = {});
/** @brief Theme used before any choice: the active theme link when one exists, else the default preset. */
Theme defaultTheme();
/** @brief Load an existing theme or atomically create theme.json from defaultTheme(). */
Theme ensureTheme(const QString &path);
/** @brief Re-read the source bound to a theme and return its resolved colors. */
Theme refreshSource(const Theme &theme);
/** @brief Directory holding named themes: $XDG_CONFIG_HOME/themes. */
QString themesDirectory();
/** @brief Named theme directories that contain a readable palette, sorted. */
QStringList staticThemeNames();
/** @brief Default link followed by "follow active", or empty when none exists. */
QString defaultActiveLink();
/** @brief Real files and directories to watch so edits, atomic replacement, and relinking reload. */
QStringList sourceWatchPaths(const QString &path);

}
