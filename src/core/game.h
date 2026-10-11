#pragma once

#include <QJsonObject>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace g2048 {

/** @brief Direction in which every tile slides. */
enum class Direction { Left, Right, Up, Down };

/** @brief One occupied cell: a stable identity for animation and its face value. */
struct Tile {
    int id = 0; ///< Unique, increasing identity; zero marks an empty cell.
    int value = 0; ///< Face value (2, 4, 8, ...); zero marks an empty cell.
    bool operator==(const Tile &) const = default;
};

/** @brief Movement of one pre-move tile to its post-move cell. */
struct Motion {
    int id = 0; ///< Identity of the moving tile before the move.
    int value = 0; ///< Face value before merging.
    int from = 0; ///< Source cell index (row * size + column).
    int to = 0; ///< Destination cell index.
    bool consumed = false; ///< True when this tile disappears into a merge at @c to.
};

/** @brief Presentation details of the most recent successful move or spawn. */
struct Turn {
    std::vector<Motion> motions; ///< Every tile that existed before the move.
    std::vector<int> merged; ///< Cell indices that now hold a freshly merged tile.
    int spawned = -1; ///< Cell index of the newly spawned tile, or -1.
    int gained = 0; ///< Points earned by this move.
};

/** @brief Undoable snapshot of board, score, and win state. */
struct Snapshot {
    std::vector<Tile> cells;
    int score = 0;
    bool won = false;
    bool operator==(const Snapshot &) const = default;
};

/**
 * @brief Classic 2048 rules on a square board.
 *
 * Tiles slide as far as possible; two equal tiles merge once per move into their
 * sum, which is added to the score. After every move that changes the board one
 * new tile appears in a random empty cell: 2 with 90% probability, otherwise 4.
 */
class Game {
public:
    static constexpr int minimumSize = 3;
    static constexpr int maximumSize = 8;
    static constexpr int maximumHistory = 32;
    static constexpr int winningValue = 2048;

    /** @brief Start a fresh game with two spawned tiles. Throws for unsupported sizes. */
    explicit Game(int size = 4, std::uint64_t seed = std::random_device{}());

    int size() const { return size_; }
    int score() const { return score_; }
    /** @brief Largest face value on the board. */
    int highest() const;
    /** @brief Number of successful moves in this game (not reduced by undo). */
    int moves() const { return moves_; }
    /** @brief Tile in a cell; index is row * size + column. */
    const Tile &at(int index) const { return cells_[index]; }
    const Tile &at(int row, int column) const { return cells_[row * size_ + column]; }
    const std::vector<Tile> &cells() const { return cells_; }
    /** @brief True once a tile reached winningValue; stays true after continuing. */
    bool won() const { return won_; }
    /** @brief True after the player chose to keep playing past the win. */
    bool continued() const { return continued_; }
    /** @brief True when no slide in any direction changes the board. */
    bool over() const;
    /** @brief True when the win should currently be announced (won and not continued). */
    bool winPending() const { return won_ && !continued_; }
    /** @brief Whether a slide in @p direction would change the board. */
    bool canMove(Direction direction) const;
    /** @brief Slide tiles; returns false and changes nothing when no tile could move. */
    bool move(Direction direction);
    /** @brief Restore the previous snapshot; returns false without history. */
    bool undo();
    bool canUndo() const { return !history_.empty(); }
    /** @brief Mark the win as acknowledged so play continues past 2048. */
    void keepPlaying() { continued_ = true; }
    /** @brief Details of the last move() or the initial spawns, for animation. */
    const Turn &lastTurn() const { return turn_; }

    /** @brief Serialize full state, including undo history and RNG-independent data. */
    QJsonObject toJson() const;
    /** @brief Validate and restore a serialized game; throws std::runtime_error. */
    static Game fromJson(const QJsonObject &data, std::uint64_t seed = std::random_device{}());

    /** @brief Test hook: replace the board with face values (0 = empty) and clear history. */
    void setValues(const std::vector<int> &values, int score = 0);

private:
    int size_;
    std::vector<Tile> cells_;
    std::vector<Snapshot> history_;
    Turn turn_;
    std::mt19937_64 random_;
    int nextId_ = 1;
    int score_ = 0;
    int moves_ = 0;
    bool won_ = false;
    bool continued_ = false;

    /** @brief Place a 2 (90%) or 4 in a random empty cell; returns its index or -1. */
    int spawn();
    Snapshot snapshot() const;
};

}
