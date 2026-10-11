/**
 * @file game.cpp
 * @brief Classic 2048 sliding, merging, spawning, undo, and serialization.
 *
 * Every direction is reduced to lines read from the edge the tiles slide
 * toward, so one routine performs all four moves. A successful move checkpoints
 * the board before mutating it. Tiles keep their identity unless they merge,
 * which gives the UI enough detail to animate each turn. Parsing validates
 * every value and assigns fresh identities, which are never serialized.
 */
#include "game.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace g2048 {
namespace {

constexpr int maximumValue = 1 << 30;
constexpr long long intLimit = std::numeric_limits<int>::max();

int checkedSize(int size) {
    if (size < Game::minimumSize || size > Game::maximumSize)
        throw std::invalid_argument("Unsupported board size");
    return size;
}

/** @brief Cell at @p position along @p line, counted from the edge tiles slide toward. */
int cellIndex(Direction direction, int size, int line, int position) {
    switch (direction) {
    case Direction::Left: return line * size + position;
    case Direction::Right: return line * size + size - 1 - position;
    case Direction::Up: return position * size + line;
    case Direction::Down: return (size - 1 - position) * size + line;
    }
    return 0;
}

bool validValue(int value) {
    return value == 0 || (value >= 2 && !(value & (value - 1)));
}

/** @brief Equal occupied tiles merge unless their sum would leave the supported range. */
bool mergeable(const Tile &first, const Tile &second) {
    return first.value && first.value == second.value && first.value < maximumValue;
}

bool reachesGoal(const std::vector<Tile> &cells) {
    return std::any_of(cells.begin(), cells.end(), [](const Tile &tile) { return tile.value >= Game::winningValue; });
}

/** @brief Empty a turn while keeping its allocated capacity. */
void reset(Turn &turn) {
    turn.motions.clear();
    turn.merged.clear();
    turn.spawned = -1;
    turn.gained = 0;
}

QJsonArray valuesArray(const std::vector<Tile> &cells) {
    QJsonArray values;
    for (const auto &tile : cells)
        values.append(tile.value);
    return values;
}

[[noreturn]] void invalid(const char *what) {
    throw std::runtime_error(std::string("Invalid saved game: ") + what);
}

int readInteger(const QJsonValue &value, double minimum, double maximum, const char *what) {
    const double number = value.toDouble();
    if (!value.isDouble() || std::floor(number) != number || number < minimum || number > maximum)
        invalid(what);
    return static_cast<int>(number);
}

bool readBool(const QJsonValue &value, const char *what) {
    if (!value.isBool())
        invalid(what);
    return value.toBool();
}

Snapshot readSnapshot(const QJsonObject &data, int count, int &nextId) {
    const auto values = data.value("values").toArray();
    if (!data.value("values").isArray() || values.size() != count)
        invalid("values");
    Snapshot snapshot;
    snapshot.cells.reserve(count);
    for (const auto &cell : values) {
        const int value = readInteger(cell, 0, maximumValue, "tile");
        if (!validValue(value))
            invalid("tile");
        snapshot.cells.push_back({value ? nextId++ : 0, value});
    }
    snapshot.score = readInteger(data.value("score"), 0, intLimit, "score");
    snapshot.won = readBool(data.value("won"), "won");
    if (!snapshot.won && reachesGoal(snapshot.cells))
        invalid("won");
    return snapshot;
}

}

Game::Game(int size, std::uint64_t seed)
    : size_(checkedSize(size)), cells_(static_cast<std::size_t>(size_ * size_)), random_(seed) {
    spawn();
    turn_.spawned = spawn();
}

int Game::highest() const {
    int result = 0;
    for (const auto &tile : cells_)
        result = std::max(result, tile.value);
    return result;
}

bool Game::over() const {
    const int count = size_ * size_;
    for (int index = 0; index < count; ++index) {
        const auto &tile = cells_[index];
        if (!tile.value || (index % size_ + 1 < size_ && mergeable(tile, cells_[index + 1]))
            || (index + size_ < count && mergeable(tile, cells_[index + size_])))
            return false;
    }
    return true;
}

bool Game::canMove(Direction direction) const {
    for (int line = 0; line < size_; ++line) {
        for (int position = 1; position < size_; ++position) {
            const auto &lead = cells_[cellIndex(direction, size_, line, position - 1)];
            const auto &tile = cells_[cellIndex(direction, size_, line, position)];
            if (tile.value && (!lead.value || mergeable(lead, tile)))
                return true;
        }
    }
    return false;
}

bool Game::move(Direction direction) {
    if (!canMove(direction))
        return false;
    if (static_cast<int>(history_.size()) >= maximumHistory)
        history_.erase(history_.begin());
    history_.push_back(snapshot());
    const auto &before = history_.back().cells;
    std::fill(cells_.begin(), cells_.end(), Tile{});
    reset(turn_);
    long long gained = 0;
    for (int line = 0; line < size_; ++line) {
        int placed = 0;
        int last = -1; // Cell of the latest placed tile while it may still absorb an equal one.
        for (int position = 0; position < size_; ++position) {
            const int from = cellIndex(direction, size_, line, position);
            const auto &tile = before[from];
            if (!tile.value)
                continue;
            if (last >= 0 && mergeable(cells_[last], tile)) {
                cells_[last] = {nextId_++, tile.value * 2};
                turn_.motions.back().consumed = true;
                turn_.motions.push_back({tile.id, tile.value, from, last, true});
                turn_.merged.push_back(last);
                gained += tile.value * 2;
                last = -1;
            } else {
                last = cellIndex(direction, size_, line, placed++);
                cells_[last] = tile;
                turn_.motions.push_back({tile.id, tile.value, from, last, false});
            }
        }
    }
    turn_.gained = static_cast<int>(std::min(gained, intLimit));
    score_ = static_cast<int>(std::min(score_ + gained, intLimit));
    won_ = won_ || reachesGoal(cells_);
    turn_.spawned = spawn();
    if (moves_ < intLimit)
        ++moves_;
    return true;
}

bool Game::undo() {
    if (history_.empty())
        return false;
    auto &previous = history_.back();
    cells_ = std::move(previous.cells);
    score_ = previous.score;
    won_ = previous.won;
    history_.pop_back();
    reset(turn_);
    return true;
}

QJsonObject Game::toJson() const {
    QJsonArray history;
    for (const auto &entry : history_)
        history.append(QJsonObject{{"values", valuesArray(entry.cells)}, {"score", entry.score}, {"won", entry.won}});
    return {{"version", 1}, {"size", size_}, {"values", valuesArray(cells_)}, {"score", score_},
            {"moves", moves_}, {"won", won_}, {"continued", continued_}, {"history", history}};
}

Game Game::fromJson(const QJsonObject &data, std::uint64_t seed) {
    readInteger(data.value("version"), 1, 1, "version");
    const int size = readInteger(data.value("size"), minimumSize, maximumSize, "size");
    const auto history = data.value("history").toArray();
    if (!data.value("history").isArray() || history.size() > maximumHistory)
        invalid("history");
    Game game(size, seed);
    game.nextId_ = 1;
    for (const auto &entry : history) {
        if (!entry.isObject())
            invalid("history");
        game.history_.push_back(readSnapshot(entry.toObject(), size * size, game.nextId_));
    }
    auto state = readSnapshot(data, size * size, game.nextId_);
    game.cells_ = std::move(state.cells);
    game.score_ = state.score;
    game.won_ = state.won;
    game.moves_ = readInteger(data.value("moves"), 0, intLimit, "moves");
    game.continued_ = readBool(data.value("continued"), "continued");
    reset(game.turn_);
    return game;
}

void Game::setValues(const std::vector<int> &values, int score) {
    if (static_cast<int>(values.size()) != size_ * size_ || score < 0
        || !std::all_of(values.begin(), values.end(), validValue))
        throw std::invalid_argument("Invalid board values");
    for (std::size_t index = 0; index < values.size(); ++index)
        cells_[index] = {values[index] ? nextId_++ : 0, values[index]};
    history_.clear();
    score_ = score;
    won_ = reachesGoal(cells_);
    reset(turn_);
}

int Game::spawn() {
    const auto empty = std::count_if(cells_.begin(), cells_.end(), [](const Tile &tile) { return !tile.value; });
    if (!empty)
        return -1;
    auto skip = std::uniform_int_distribution<std::ptrdiff_t>(0, empty - 1)(random_);
    const int value = std::uniform_int_distribution<int>(0, 9)(random_) ? 2 : 4;
    for (int index = 0; index < size_ * size_; ++index) {
        if (!cells_[index].value && !skip--) {
            cells_[index] = {nextId_++, value};
            return index;
        }
    }
    return -1;
}

Snapshot Game::snapshot() const {
    return {cells_, score_, won_};
}

}
