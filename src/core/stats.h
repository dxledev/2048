#pragma once

#include "game.h"
#include <QJsonObject>
#include <QString>
#include <array>
#include <deque>
#include <map>

namespace g2048 {

/**
 * @brief One game as seen by the statistics: the live game or a finished one.
 *
 * Scores are final (undo lowers them); the highest tile, moves, merges, and
 * undos only ever grow. A game is finished when a new game replaces it.
 */
struct GameRecord {
    int size = 4;
    qint64 started = 0; ///< Wall-clock start, milliseconds since the Unix epoch.
    qint64 played = 0; ///< Active play time in milliseconds (idle gaps are capped).
    qint64 lastActive = 0; ///< Time of the last recorded action, or 0 while suspended.
    int score = 0; ///< Score after the last recorded action.
    int highest = 0; ///< Largest tile this game ever held.
    int moves = 0; ///< Successful moves, matching Game::moves().
    int undos = 0;
    int merges = 0;
    int wonAtMove = 0; ///< Move count when 2048 first appeared, or 0 if never.
    bool over = false; ///< No move was possible after the last recorded action.
    bool won() const { return wonAtMove > 0; }
    bool operator==(const GameRecord &) const = default;
};

/** @brief Aggregates over finished games plus the live game, for one size or all sizes. */
struct Totals {
    int games = 0; ///< Games with at least one move.
    int wins = 0; ///< Games that reached 2048.
    int losses = 0; ///< Games that ended with no moves left and never reached 2048.
    long long score = 0; ///< Sum of final scores, for averages.
    long long moves = 0;
    long long undos = 0;
    long long merges = 0;
    qint64 played = 0; ///< Active play time in milliseconds.
    int bestScore = 0;
    int bestTile = 0;
    int fastestWin = 0; ///< Fewest moves to reach 2048, or 0 without a win.
    std::map<int, int> peaks; ///< Highest tile -> number of games that peaked at it.
    /** @brief Games whose highest tile was at least @p value. */
    int reached(int value) const;
    double winRate() const { return games ? double(wins) / games : 0.0; }
    double averageScore() const { return games ? double(score) / games : 0.0; }
};

/**
 * @brief Lifetime play statistics, persisted beside the session in stats.json.
 *
 * The window reports every successful move and undo together with the current
 * wall-clock time. Time between consecutive actions counts as play time, capped
 * at idleLimit so a board left open overnight adds at most a minute.
 */
class Stats {
public:
    static constexpr int maximumRecent = 30;
    static constexpr qint64 idleLimit = 60'000;

    /** @brief Finish the live record (kept only if it has moves) and start one for @p game. */
    void begin(const Game &game, qint64 now);
    /** @brief Keep the live record if it describes @p game (same size and moves), else begin(). */
    void attach(const Game &game, qint64 now);
    /** @brief Account for a successful move; @p game is the state after it. */
    void recordMove(const Game &game, Direction direction, qint64 now);
    /** @brief Account for a successful undo; @p game is the state after it. */
    void recordUndo(const Game &game, qint64 now);
    /** @brief Stop the play clock; the next action starts a fresh interval. */
    void suspend() { current_.lastActive = 0; }
    /** @brief Forget everything and start a live record for @p game. */
    void reset(const Game &game, qint64 now);

    const GameRecord &current() const { return current_; }
    /** @brief Finished games with at least one move, oldest first, at most maximumRecent. */
    const std::deque<GameRecord> &recent() const { return recent_; }
    /** @brief Totals for one board size, or every size when @p size is 0; includes the live game. */
    Totals totals(int size = 0) const;
    /** @brief Consecutive won games ending with the latest (the live game counts once won). */
    int currentStreak() const;
    int bestStreak() const;
    /** @brief Successful moves per direction, indexed by Direction. */
    const std::array<long long, 4> &directions() const { return directions_; }
    long long spawnedTwos() const { return twos_; }
    long long spawnedFours() const { return fours_; }
    /** @brief When statistics began, milliseconds since the Unix epoch, or 0. */
    qint64 since() const { return since_; }

    QJsonObject toJson() const;
    /** @brief Validate and restore serialized statistics; throws std::runtime_error. */
    static Stats fromJson(const QJsonObject &data);

private:
    GameRecord current_;
    std::deque<GameRecord> recent_;
    std::map<int, Totals> finished_; ///< Finished games per board size.
    std::array<long long, 4> directions_{};
    long long twos_ = 0, fours_ = 0;
    int streak_ = 0; ///< Trailing won finished games.
    int bestStreak_ = 0; ///< Best streak among finished games.
    qint64 since_ = 0;
};

/** @brief Atomically write statistics to @p path. */
void saveStats(const QString &path, const Stats &stats);
/** @brief Read statistics from @p path; throws std::runtime_error when missing or invalid. */
Stats loadStats(const QString &path);

}
