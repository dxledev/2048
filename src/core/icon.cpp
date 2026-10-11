/**
 * @file icon.cpp
 * @brief Themed SVG application icon kept next to theme.json.
 *
 * Launchers read the icon from disk, so it is regenerated whenever the theme
 * changes; desktop entries that point at it recolor along with the game.
 */
#include "icon.h"
#include "color.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <stdexcept>

namespace g2048 {

QByteArray iconSvg(const Theme &theme) {
    const std::array steps{1, 4, 7, 10};
    QString tiles;
    for (int index = 0; index < 4; ++index) {
        tiles += QString(R"(  <rect x="%1" y="%2" width="40" height="40" rx="9" fill="%3"/>
)").arg(20 + index % 2 * 48).arg(20 + index / 2 * 48).arg(theme.tiles[steps[index]].name());
    }
    return QString(R"(<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <rect width="128" height="128" rx="28" fill="%1"/>
%2</svg>
)").arg(opaque(theme.color("surface")).name(), tiles).toUtf8();
}

void writeIcon(const QString &path, const Theme &theme) {
    const auto contents = iconSvg(theme);
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() < 65536 && existing.readAll() == contents)
        return;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error(("Cannot create " + QFileInfo(path).absolutePath()).toStdString());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        throw std::runtime_error((path + ": " + file.errorString()).toStdString());
}

QString iconPath(const QString &configDirectory) {
    return configDirectory + "/icon.svg";
}

}
