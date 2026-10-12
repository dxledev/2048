#pragma once

#include <QJsonObject>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace g2048 {

/** @brief Direction in which every tile slides. */
enum class Direction { Left, Right, Up, Down };

/** @brief Limited-use power-ups; each starts with its startingCharges and is earned back by making its reward tile. */
enum class Power { Undo, Swap, Remove };

/**
 * @brief Power-ups mode has limited undo, swap, and remove; Classic is the original game with none of them;
 * Casual has unlimited use of all three. The values index per-mode data, so new modes are only appended.
 */
enum class Mode { PowerUps, Classic, Casual };

/** @brief Number of game modes. */
inline constexpr int modeCount = 3;
/** @brief Every mode in Mode order. */
inline constexpr std::array<Mode, modeCount> allModes{Mode::PowerUps, Mode::Classic, Mode::Casual};

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
    std::array<int, 3> earned{}; ///< Charges gained this turn, indexed by Power.
};

/** @brief Undoable snapshot of board, score, move count, win state, and charges. */
struct Snapshot {
    std::vector<Tile> cells;
    int score = 0;
    int moves = 0;
    bool won = false;
    std::array<int, 3> charges{};
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
    static constexpr int maximumSize = 5;
    static constexpr int maximumHistory = 32;
    static constexpr int winningValue = 2048;
    static constexpr int powerCount = 3;
    static constexpr int maximumCharges = 2;

    /** @brief Charges of @p power a fresh Standard game starts with: 2 Undo, 1 Swap, 0 Delete. */
    static constexpr int startingCharges(Power power) {
        switch (power) {
        case Power::Undo: return 2;
        case Power::Swap: return 1;
        case Power::Remove: return 0;
        }
        return 0;
    }

    /** @brief Tile value whose creation by a merge earns one charge of @p power: 128, 256, 512. */
    static constexpr int rewardValue(Power power) {
        switch (power) {
        case Power::Undo: return 128;
        case Power::Swap: return 256;
        case Power::Remove: return 512;
        }
        return 0;
    }

    /** @brief Start a fresh game with two spawned tiles. Throws for unsupported sizes. */
    explicit Game(int size = 4, Mode mode = Mode::PowerUps, std::uint64_t seed = std::random_device{}());

    int size() const { return size_; }
    Mode mode() const { return mode_; }
    /** @brief True when power-ups cost no charges (Casual mode). */
    bool unlimitedPowers() const { return mode_ == Mode::Casual; }
    /** @brief True when the mode offers undo, swap, and remove at all (Standard and Casual, not Classic). */
    bool hasPowers() const { return mode_ != Mode::Classic; }
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
    /** @brief Restore the previous snapshot; returns false without history or, outside Casual, an Undo charge. */
    bool undo();
    bool canUndo() const { return !history_.empty() && hasCharge(Power::Undo); }
    /** @brief Remaining charges of @p power. */
    int charges(Power power) const;
    /** @brief Whether the tiles in cells @p first and @p second may be exchanged. */
    bool canSwap(int first, int second) const;
    /** @brief Exchange two occupied, unequal tiles; returns false and changes nothing when not allowed. */
    bool swap(int first, int second);
    /** @brief Whether a tile with face value @p value exists and a Remove charge is available (always, in Casual). */
    bool canRemove(int value) const;
    /** @brief Clear every tile with face value @p value; returns false and changes nothing when not allowed. */
    bool remove(int value);
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
    /** @brief Test hook: set the charges of @p power, clamped to [0, maximumCharges]. */
    void setCharges(Power power, int count);

private:
    int size_;
    Mode mode_;
    std::vector<Tile> cells_;
    std::vector<Snapshot> history_;
    Turn turn_;
    std::mt19937_64 random_;
    int nextId_ = 1;
    int score_ = 0;
    int moves_ = 0;
    bool won_ = false;
    bool continued_ = false;
    std::array<int, powerCount> charges_{};

    /** @brief Whether @p power may be used now: a charge is left, or the mode has unlimited powers. */
    bool hasCharge(Power power) const { return unlimitedPowers() || charges_[static_cast<int>(power)] > 0; }
    /** @brief Place a 2 (90%) or 4 in a random empty cell; returns its index or -1. */
    int spawn();
    /** @brief Push the current state onto the undo history, dropping the oldest entry when full. */
    void checkpoint();
    Snapshot snapshot() const;
};

}
