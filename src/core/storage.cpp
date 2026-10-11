/**
 * @file storage.cpp
 * @brief Atomic JSON persistence for the game session and shared JSON files.
 *
 * Every write goes through QSaveFile, so readers such as the theme watcher
 * only ever observe a complete previous or complete new file.
 */
#include "storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

namespace g2048 {
namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::runtime_error(message.toStdString());
}

}

QString defaultConfigDirectory() {
    const QString override = qEnvironmentVariable("GAME_2048_CONFIG_DIR");
    if (!override.isEmpty())
        return QDir(override).absolutePath();
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/2048";
}

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(path + ": " + file.errorString());
    if (file.size() > 1024 * 1024)
        fail(path + ": JSON file exceeds 1 MiB");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail(path + ": expected a valid JSON object (" + error.errorString() + ")");
    return document.object();
}

void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail("Cannot create " + QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(path + ": " + file.errorString());
    const auto bytes = QJsonDocument(data).toJson(format);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(path + ": " + file.errorString());
}

void saveSession(const QString &path, const Game &game, const BestScores &best) {
    QJsonObject scores;
    for (auto entry = best.constBegin(); entry != best.constEnd(); ++entry)
        scores.insert(QString::number(entry.key()), entry.value());
    writeJson(path, {{"version", 1}, {"game", game.toJson()}, {"best", scores}}, QJsonDocument::Compact);
}

Game loadSession(const QString &path, BestScores &best) {
    const auto data = readJson(path);
    if (data.value("version").toInt() != 1)
        fail("Unsupported session version");
    if (!data.value("game").isObject() || !data.value("best").isObject())
        fail("Invalid session layout");
    BestScores scores;
    const auto saved = data.value("best").toObject();
    for (auto entry = saved.constBegin(); entry != saved.constEnd(); ++entry) {
        bool valid = false;
        const int size = entry.key().toInt(&valid);
        const double score = entry.value().toDouble(-1);
        if (!valid || size < Game::minimumSize || size > Game::maximumSize
            || score < 0 || score > 2147483647.0 || score != int(score))
            fail("Invalid best score");
        scores.insert(size, int(score));
    }
    auto game = Game::fromJson(data.value("game").toObject());
    scores[game.size()] = std::max(scores.value(game.size()), game.score());
    best = std::move(scores);
    return game;
}

}
