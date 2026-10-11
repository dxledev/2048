#pragma once

#include "game.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace g2048 {

/** @brief Best score reached for each board size. */
using BestScores = QMap<int, int>;

/** @brief Resolve the configuration directory, honoring GAME_2048_CONFIG_DIR. */
QString defaultConfigDirectory();
/** @brief Read a JSON object from disk and throw on I/O, size, or format errors. */
QJsonObject readJson(const QString &path);
/** @brief Atomically write a JSON object, creating parent directories as needed. */
void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format = QJsonDocument::Indented);
/** @brief Persist the current game and best scores. */
void saveSession(const QString &path, const Game &game, const BestScores &best);
/** @brief Restore a saved game and best scores; throws std::runtime_error when invalid. */
Game loadSession(const QString &path, BestScores &best);

}
