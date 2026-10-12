/**
 * @file stats.cpp
 * @brief Lifetime statistics: the live game record, finished totals, and persistence.
 *
 * Finished games are folded into per-size totals as they end, so the totals
 * stay exact however many games were played, while only the latest few full
 * records are kept for charts. The live game is never folded in until a new
 * game replaces it; totals() adds it on the fly, which lets undo keep lowering
 * its score without ever double counting. Parsing validates every number and
 * the invariants between them, because the file is hand-editable.
 */
#include "stats.h"
#include "storage.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace g2048 {
namespace {

constexpr double intLimit = std::numeric_limits<int>::max();
constexpr double countLimit = 9007199254740992.0; // 2^53: every integer below is exact in a JSON double.
constexpr int maximumTile = 1 << 30;

[[noreturn]] void invalid(const std::string &what) {
    throw std::runtime_error("Invalid statistics: " + what);
}

long long readCount(const QJsonValue &value, double maximum, const std::string &what) {
    const double number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number || number < 0 || number > maximum)
        invalid(what);
    return static_cast<long long>(number);
}

int readInt(const QJsonValue &value, const std::string &what, double minimum = 0, double maximum = intLimit) {
    const auto number = readCount(value, maximum, what);
    if (number < minimum)
        invalid(what);
    return static_cast<int>(number);
}

int readTile(const QJsonValue &value, const std::string &what) {
    const int tile = readInt(value, what, 0, maximumTile);
    if (tile == 1 || (tile & (tile - 1)))
        invalid(what);
    return tile;
}

/** @brief True for a board size that is numeric but no longer offered; such data is dropped, not rejected. */
bool retired(const QJsonValue &size) {
    return size.isDouble() && size.toDouble() > Game::maximumSize;
}

QJsonObject readObject(const QJsonValue &value, const std::string &what) {
    if (!value.isObject())
        invalid(what);
    return value.toObject();
}

int saturate(long long value) {
    return static_cast<int>(std::min<long long>(value, std::numeric_limits<int>::max()));
}

/** @brief Count active time since the last action, capped so idle gaps add little. */
void tick(GameRecord &record, qint64 now) {
    if (record.lastActive > 0)
        record.played += std::clamp<qint64>(now - record.lastActive, 0, Stats::idleLimit);
    record.lastActive = now;
}

/** @brief Totals contributed by a single game. */
Totals single(const GameRecord &record) {
    Totals totals;
    totals.games = 1;
    totals.wins = record.won();
    totals.losses = record.over && !record.won();
    totals.score = record.score;
    totals.moves = record.moves;
    totals.undos = record.undos;
    totals.merges = record.merges;
    totals.played = record.played;
    totals.bestScore = record.score;
    totals.bestTile = record.highest;
    totals.fastestWin = record.wonAtMove;
    totals.peaks[record.highest] = 1;
    return totals;
}

void fold(Totals &into, const Totals &from) {
    into.games += from.games;
    into.wins += from.wins;
    into.losses += from.losses;
    into.score += from.score;
    into.moves += from.moves;
    into.undos += from.undos;
    into.merges += from.merges;
    into.played += from.played;
    into.bestScore = std::max(into.bestScore, from.bestScore);
    into.bestTile = std::max(into.bestTile, from.bestTile);
    if (from.fastestWin && (!into.fastestWin || from.fastestWin < into.fastestWin))
        into.fastestWin = from.fastestWin;
    for (const auto &[tile, games] : from.peaks)
        into.peaks[tile] += games;
}

QJsonObject recordJson(const GameRecord &record) {
    return {{"size", record.size}, {"started", double(record.started)}, {"played", double(record.played)},
            {"lastActive", double(record.lastActive)}, {"score", record.score}, {"highest", record.highest},
            {"moves", record.moves}, {"undos", record.undos}, {"merges", record.merges},
            {"wonAtMove", record.wonAtMove}, {"over", record.over}};
}

GameRecord readRecord(const QJsonValue &value, const std::string &what) {
    const auto data = readObject(value, what);
    GameRecord record;
    record.size = readInt(data.value("size"), what + " size", Game::minimumSize, Game::maximumSize);
    record.started = readCount(data.value("started"), countLimit, what + " start");
    record.played = readCount(data.value("played"), countLimit, what + " play time");
    record.lastActive = readCount(data.value("lastActive"), countLimit, what + " last action");
    record.score = readInt(data.value("score"), what + " score");
    record.highest = readTile(data.value("highest"), what + " highest tile");
    record.moves = readInt(data.value("moves"), what + " moves");
    record.undos = readInt(data.value("undos"), what + " undos");
    record.merges = readInt(data.value("merges"), what + " merges");
    record.wonAtMove = readInt(data.value("wonAtMove"), what + " win move", 0, std::max(record.moves, 1));
    if (!data.value("over").isBool())
        invalid(what + " over");
    record.over = data.value("over").toBool();
    return record;
}

QJsonObject totalsJson(const Totals &totals) {
    QJsonObject peaks;
    for (const auto &[tile, games] : totals.peaks)
        peaks.insert(QString::number(tile), games);
    return {{"games", totals.games}, {"wins", totals.wins}, {"losses", totals.losses},
            {"score", double(totals.score)}, {"moves", double(totals.moves)}, {"undos", double(totals.undos)},
            {"merges", double(totals.merges)}, {"played", double(totals.played)}, {"bestScore", totals.bestScore},
            {"bestTile", totals.bestTile}, {"fastestWin", totals.fastestWin}, {"peaks", peaks}};
}

Totals readTotals(const QJsonValue &value, const std::string &what) {
    const auto data = readObject(value, what);
    Totals totals;
    totals.games = readInt(data.value("games"), what + " games", 1);
    totals.wins = readInt(data.value("wins"), what + " wins");
    totals.losses = readInt(data.value("losses"), what + " losses");
    totals.score = readCount(data.value("score"), countLimit, what + " score");
    totals.moves = readCount(data.value("moves"), countLimit, what + " moves");
    totals.undos = readCount(data.value("undos"), countLimit, what + " undos");
    totals.merges = readCount(data.value("merges"), countLimit, what + " merges");
    totals.played = readCount(data.value("played"), countLimit, what + " play time");
    totals.bestScore = readInt(data.value("bestScore"), what + " best score");
    totals.bestTile = readTile(data.value("bestTile"), what + " best tile");
    totals.fastestWin = readInt(data.value("fastestWin"), what + " fastest win");
    const auto peaks = readObject(data.value("peaks"), what + " peaks");
    long long counted = 0;
    for (auto entry = peaks.constBegin(); entry != peaks.constEnd(); ++entry) {
        bool valid = false;
        const int tile = entry.key().toInt(&valid);
        if (!valid || entry.key() != QString::number(tile))
            invalid(what + " peak tile");
        readTile(tile, what + " peak tile");
        const int games = readInt(entry.value(), what + " peak count", 1);
        totals.peaks[tile] = games;
        counted += games;
    }
    if (totals.wins + totals.losses > totals.games || counted != totals.games
        || totals.bestScore > totals.score || totals.moves < totals.games
        || (totals.peaks.empty() ? 0 : totals.peaks.rbegin()->first) != totals.bestTile
        || (totals.fastestWin > 0) != (totals.wins > 0) || totals.fastestWin > totals.moves)
        invalid(what + " totals are inconsistent");
    return totals;
}

}

int Totals::reached(int value) const {
    int games = 0;
    for (auto entry = peaks.lower_bound(value); entry != peaks.end(); ++entry)
        games += entry->second;
    return games;
}

void Stats::finish() {
    if (current_.moves > 0) {
        recent_.push_back(current_);
        if (static_cast<int>(recent_.size()) > maximumRecent)
            recent_.pop_front();
        fold(finished_[current_.size], single(current_));
        streak_ = current_.won() ? streak_ + 1 : 0;
        bestStreak_ = std::max(bestStreak_, streak_);
    }
    current_ = GameRecord{};
}

void Stats::begin(const Game &game, qint64 now) {
    finish();
    if (!since_)
        since_ = now;
    current_.size = game.size();
    current_.started = now;
    current_.lastActive = now;
    current_.score = game.score();
    current_.highest = game.highest();
    current_.moves = game.moves();
    current_.over = game.over();
    current_.wonAtMove = game.won() ? std::max(1, game.moves()) : 0;
    if (!game.moves()) {
        for (const auto &tile : game.cells()) {
            twos_ += tile.value == 2;
            fours_ += tile.value == 4;
        }
    }
}

void Stats::attach(const Game &game, qint64 now) {
    if (current_.size != game.size() || current_.moves < game.moves() || !since_) {
        begin(game, now);
        return;
    }
    current_.score = game.score();
    current_.highest = std::max(current_.highest, game.highest());
    current_.over = game.over();
    if (game.won() && !current_.wonAtMove)
        current_.wonAtMove = std::max(1, game.moves());
    current_.lastActive = 0;
}

void Stats::recordMove(const Game &game, Direction direction, qint64 now) {
    tick(current_, now);
    ++directions_[static_cast<int>(direction)];
    const auto &turn = game.lastTurn();
    current_.merges = saturate(current_.merges + static_cast<long long>(turn.merged.size()));
    if (turn.spawned >= 0) {
        twos_ += game.at(turn.spawned).value == 2;
        fours_ += game.at(turn.spawned).value == 4;
    }
    current_.score = game.score();
    current_.highest = std::max(current_.highest, game.highest());
    current_.moves = saturate(current_.moves + 1LL);
    current_.over = game.over();
    if (game.won() && !current_.wonAtMove)
        current_.wonAtMove = std::max(1, game.moves());
}

void Stats::recordUndo(const Game &game, qint64 now) {
    tick(current_, now);
    current_.undos = saturate(current_.undos + 1LL);
    current_.score = game.score();
    current_.over = game.over();
}

void Stats::reset(const Game &game, qint64 now) {
    *this = Stats{};
    since_ = now;
    begin(game, now);
}

Totals Stats::totals(int size) const {
    Totals result;
    for (const auto &[boardSize, totals] : finished_) {
        if (!size || boardSize == size)
            fold(result, totals);
    }
    if (current_.moves > 0 && (!size || current_.size == size))
        fold(result, single(current_));
    return result;
}

int Stats::currentStreak() const {
    if (current_.moves > 0 && current_.over && !current_.won())
        return 0;
    return streak_ + (current_.won() ? 1 : 0);
}

int Stats::bestStreak() const {
    return std::max(bestStreak_, currentStreak());
}

QJsonObject Stats::toJson() const {
    QJsonArray recent;
    for (const auto &record : recent_)
        recent.append(recordJson(record));
    QJsonObject sizes;
    for (const auto &[size, totals] : finished_)
        sizes.insert(QString::number(size), totalsJson(totals));
    QJsonArray directions;
    for (const auto count : directions_)
        directions.append(double(count));
    return {{"version", 1}, {"since", double(since_)}, {"current", recordJson(current_)}, {"recent", recent},
            {"sizes", sizes}, {"directions", directions},
            {"spawns", QJsonObject{{"2", double(twos_)}, {"4", double(fours_)}}},
            {"streak", streak_}, {"bestStreak", bestStreak_}};
}

Stats Stats::fromJson(const QJsonObject &data) {
    readInt(data.value("version"), "version", 1, 1);
    Stats stats;
    stats.since_ = readCount(data.value("since"), countLimit, "start time");
    // A live game on a retired board is replaced by an empty record so it is never finished into totals.
    if (!retired(data.value("current").toObject().value("size")))
        stats.current_ = readRecord(data.value("current"), "current game");
    if (!data.value("recent").isArray())
        invalid("recent games");
    const auto recent = data.value("recent").toArray();
    if (recent.size() > maximumRecent)
        invalid("too many recent games");
    for (const auto &entry : recent) {
        if (retired(entry.toObject().value("size")))
            continue;
        stats.recent_.push_back(readRecord(entry, "recent game"));
        if (!stats.recent_.back().moves)
            invalid("recent game without moves");
    }
    long long games = 0;
    long long kept = 0;
    const auto sizes = readObject(data.value("sizes"), "sizes");
    for (auto entry = sizes.constBegin(); entry != sizes.constEnd(); ++entry) {
        bool valid = false;
        const int size = entry.key().toInt(&valid);
        if (!valid || entry.key() != QString::number(size) || size < Game::minimumSize)
            invalid("board size");
        const auto totals = readTotals(entry.value(), "size " + std::to_string(size));
        games += totals.games;
        if (size > Game::maximumSize)
            continue;
        stats.finished_[size] = totals;
        kept += totals.games;
    }
    if (kept < static_cast<long long>(stats.recent_.size()))
        invalid("more recent games than finished games");
    const auto directions = data.value("directions").toArray();
    if (!data.value("directions").isArray() || directions.size() != 4)
        invalid("directions");
    for (int index = 0; index < 4; ++index)
        stats.directions_[index] = readCount(directions[index], countLimit, "direction count");
    const auto spawns = readObject(data.value("spawns"), "spawns");
    stats.twos_ = readCount(spawns.value("2"), countLimit, "spawned twos");
    stats.fours_ = readCount(spawns.value("4"), countLimit, "spawned fours");
    stats.streak_ = readInt(data.value("streak"), "streak");
    stats.bestStreak_ = readInt(data.value("bestStreak"), "best streak");
    if (stats.streak_ > stats.bestStreak_ || stats.bestStreak_ > games)
        invalid("streaks");
    return stats;
}

void saveStats(const QString &path, const Stats &stats) {
    writeJson(path, stats.toJson(), QJsonDocument::Compact);
}

Stats loadStats(const QString &path) {
    return Stats::fromJson(readJson(path));
}

}
