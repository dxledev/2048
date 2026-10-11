#pragma once

#include "theme.h"

namespace g2048 {

/** @brief Scalable application icon drawn in the theme's colors. */
QByteArray iconSvg(const Theme &theme);
/** @brief Atomically write iconSvg() to @p path unless the file already matches. */
void writeIcon(const QString &path, const Theme &theme);
/** @brief Location of the themed icon inside a configuration directory. */
QString iconPath(const QString &configDirectory);

}
