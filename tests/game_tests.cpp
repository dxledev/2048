#include "core/game.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <algorithm>
#include <random>
#include <set>

using namespace g2048;

namespace {

constexpr Direction directions[] = {Direction::Left, Direction::Right, Direction::Up, Direction::Down};

QList<int> valuesOf(const Game &game) {
    QList<int> values;
    for (const auto &tile : game.cells())
        values.append(tile.value);
    return values;
}

/** @brief Board values with the tile spawned by the last move removed. */
QList<int> withoutSpawn(const Game &game) {
    auto values = valuesOf(game);
    if (game.lastTurn().spawned >= 0)
        values[game.lastTurn().spawned] = 0;
    return values;
}

int tileCount(const Game &game) {
    return static_cast<int>(std::count_if(game.cells().begin(), game.cells().end(), [](const Tile &tile) { return tile.value; }));
}

void load(Game &game, const QList<int> &values, int score = 0) {
    game.setValues(std::vector<int>(values.begin(), values.end()), score);
}

/** @brief Board holding @p line on line @p line read from the edge tiles slide toward. */
QList<int> board(Direction direction, int line, const QList<int> &values, int size = 4) {
    QList<int> result(size * size, 0);
    for (int position = 0; position < values.size(); ++position) {
        const int index = direction == Direction::Left ? line * size + position
            : direction == Direction::Right ? line * size + size - 1 - position
            : direction == Direction::Up ? position * size + line : (size - 1 - position) * size + line;
        result[index] = values[position];
    }
    return result;
}

bool randomMove(Game &game, std::mt19937 &random) {
    std::vector<Direction> options;
    for (auto direction : directions) {
        if (game.canMove(direction))
            options.push_back(direction);
    }
    if (options.empty())
        return false;
    return game.move(options[std::uniform_int_distribution<std::size_t>(0, options.size() - 1)(random)]);
}

bool uniqueIds(const Game &game) {
    std::set<int> ids;
    for (const auto &tile : game.cells()) {
        if ((tile.value == 0) != (tile.id == 0) || (tile.id && !ids.insert(tile.id).second))
            return false;
    }
    return true;
}

QString describe(const std::vector<Motion> &motions, int id) {
    const auto found = std::find_if(motions.begin(), motions.end(), [id](const Motion &motion) { return motion.id == id; });
    if (found == motions.end())
        return "missing";
    return QString("%1 %2->%3%4").arg(found->value).arg(found->from).arg(found->to).arg(found->consumed ? " consumed" : "");
}

}

class GameTests : public QObject {
    Q_OBJECT
private slots:
    void slides_data() {
        QTest::addColumn<int>("direction");
        QTest::addColumn<QList<int>>("line");
        QTest::addColumn<QList<int>>("expected");
        QTest::addColumn<int>("gained");
        const struct {
            const char *name;
            QList<int> line, expected;
            int gained;
        } cases[] = {
            {"pairs", {2, 2, 2, 2}, {4, 4, 0, 0}, 8},
            {"no double merge", {2, 2, 4, 0}, {4, 4, 0, 0}, 4},
            {"two pairs", {4, 4, 8, 8}, {8, 16, 0, 0}, 24},
            {"gap", {2, 0, 2, 4}, {4, 4, 0, 0}, 4},
            {"triple", {2, 2, 2, 0}, {4, 2, 0, 0}, 4},
            {"slide", {0, 0, 0, 2}, {2, 0, 0, 0}, 0},
        };
        const char *names[] = {"left", "right", "up", "down"};
        for (int direction = 0; direction < 4; ++direction) {
            for (const auto &entry : cases)
                QTest::addRow("%s %s", names[direction], entry.name) << direction << entry.line << entry.expected << entry.gained;
        }
    }

    void slides() {
        QFETCH(int, direction);
        QFETCH(QList<int>, line);
        QFETCH(QList<int>, expected);
        QFETCH(int, gained);
        const auto towards = static_cast<Direction>(direction);
        Game game(4, 7);
        load(game, board(towards, 1, line), 100);
        QVERIFY(game.canMove(towards));
        QVERIFY(game.move(towards));
        const auto result = board(towards, 1, expected);
        QCOMPARE(withoutSpawn(game), result);
        const int spawned = game.lastTurn().spawned;
        QVERIFY(spawned >= 0);
        QCOMPARE(result[spawned], 0);
        QVERIFY(game.at(spawned).value == 2 || game.at(spawned).value == 4);
        QCOMPARE(game.lastTurn().gained, gained);
        QCOMPARE(game.score(), 100 + gained);
        QCOMPARE(game.moves(), 1);
        QVERIFY(game.canUndo());
        QVERIFY(uniqueIds(game));
    }

    void noOpMove() {
        Game game(4, 3);
        load(game, {2, 4, 8, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 5);
        const auto before = game.cells();
        QVERIFY(!game.canMove(Direction::Left));
        QVERIFY(!game.canMove(Direction::Right));
        QVERIFY(!game.canMove(Direction::Up));
        QVERIFY(game.canMove(Direction::Down));
        for (auto direction : {Direction::Left, Direction::Right, Direction::Up})
            QVERIFY(!game.move(direction));
        QVERIFY(game.cells() == before);
        QCOMPARE(game.score(), 5);
        QCOMPARE(game.moves(), 0);
        QVERIFY(!game.canUndo());
        QVERIFY(game.lastTurn().motions.empty());
        QCOMPARE(game.lastTurn().spawned, -1);

        load(game, {8, 16, 8, 16, 16, 8, 16, 8, 8, 16, 8, 16, 0, 0, 0, 64});
        QVERIFY(game.move(Direction::Left));
        const auto after = game.cells();
        const auto turn = game.lastTurn();
        QCOMPARE(turn.motions.size(), std::size_t(13));
        QVERIFY(turn.spawned > 12);
        QVERIFY(!game.canMove(Direction::Up));
        QVERIFY(!game.move(Direction::Up));
        QVERIFY(game.cells() == after);
        QCOMPARE(game.lastTurn().motions.size(), turn.motions.size());
        QCOMPARE(game.lastTurn().spawned, turn.spawned);
        QCOMPARE(game.lastTurn().gained, turn.gained);
        QCOMPARE(game.moves(), 1);
        QVERIFY(game.undo());
        QVERIFY(!game.canUndo());
    }

    void spawning() {
        Game game(4, 11);
        QCOMPARE(tileCount(game), 2);
        const auto &start = game.lastTurn();
        QVERIFY(start.motions.empty());
        QVERIFY(start.merged.empty());
        QCOMPARE(start.gained, 0);
        QVERIFY(start.spawned >= 0 && start.spawned < 16);
        QVERIFY(game.at(start.spawned).value == 2 || game.at(start.spawned).value == 4);
        QVERIFY(!game.canUndo());
        QCOMPARE(game.moves(), 0);
        QCOMPARE(game.score(), 0);
        QVERIFY(uniqueIds(game));

        load(game, {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Right));
        QCOMPARE(tileCount(game), 2);
        QCOMPARE(game.at(3).value, 2);
        QVERIFY(game.lastTurn().spawned != 3);

        Game small(3, 12);
        load(small, {2, 4, 8, 16, 32, 64, 128, 0, 256});
        QVERIFY(small.move(Direction::Left));
        QCOMPARE(small.lastTurn().spawned, 8);
        QCOMPARE(tileCount(small), 9);

        std::mt19937 random(2);
        for (int step = 0; step < 300 && !game.over(); ++step) {
            const int before = tileCount(game);
            QVERIFY(randomMove(game, random));
            QCOMPARE(tileCount(game), before - static_cast<int>(game.lastTurn().merged.size()) + 1);
        }
    }

    void scoring() {
        Game game(4, 5);
        load(game, {2, 2, 0, 0, 4, 4, 0, 0, 8, 8, 8, 0, 0, 0, 0, 0}, 10);
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.lastTurn().gained, 4 + 8 + 16);
        QCOMPARE(game.score(), 38);
        int total = game.score();
        std::mt19937 random(1);
        for (int step = 0; step < 200 && randomMove(game, random); ++step) {
            int sum = 0;
            for (int cell : game.lastTurn().merged)
                sum += game.at(cell).value;
            QCOMPARE(game.lastTurn().gained, sum);
            total += sum;
            QCOMPARE(game.score(), total);
        }
        QVERIFY(total > 38);
    }

    void winning() {
        Game game(4, 9);
        load(game, {1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!game.won());
        QVERIFY(!game.winPending());
        QCOMPARE(game.highest(), 1024);
        QVERIFY(game.move(Direction::Left));
        QVERIFY(game.won());
        QVERIFY(game.winPending());
        QVERIFY(!game.continued());
        QCOMPARE(game.highest(), 2048);
        game.keepPlaying();
        QVERIFY(game.won());
        QVERIFY(game.continued());
        QVERIFY(!game.winPending());
        QVERIFY(game.move(Direction::Right));
        QVERIFY(game.won());
        QVERIFY(!game.winPending());

        Game fresh(4, 9);
        load(fresh, {1024, 512, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!fresh.won());
        load(fresh, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4096});
        QVERIFY(fresh.won());
        QVERIFY(fresh.winPending());
        QCOMPARE(fresh.highest(), 4096);
    }

    void gameOver() {
        const QList<int> stuck = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};
        Game game(4, 2);
        load(game, stuck);
        QVERIFY(game.over());
        for (auto direction : directions) {
            QVERIFY(!game.canMove(direction));
            QVERIFY(!game.move(direction));
        }
        QCOMPARE(valuesOf(game), stuck);

        auto horizontal = stuck;
        horizontal[0] = horizontal[1] = 8;
        load(game, horizontal);
        QVERIFY(!game.over());
        QVERIFY(game.canMove(Direction::Left));
        QVERIFY(game.canMove(Direction::Right));
        QVERIFY(!game.canMove(Direction::Up));
        QVERIFY(!game.canMove(Direction::Down));

        auto vertical = stuck;
        vertical[11] = vertical[15] = 8;
        load(game, vertical);
        QVERIFY(!game.over());
        QVERIFY(!game.canMove(Direction::Left));
        QVERIFY(!game.canMove(Direction::Right));
        QVERIFY(game.canMove(Direction::Up));
        QVERIFY(game.canMove(Direction::Down));

        auto gap = stuck;
        gap[5] = 0;
        load(game, gap);
        QVERIFY(!game.over());

        Game small(3, 1);
        load(small, {2, 4, 2, 4, 2, 4, 2, 4, 2});
        QVERIFY(small.over());
    }

    void largestValues() {
        constexpr int top = 1 << 30;
        Game game(4, 6);
        load(game, {top, top, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!game.canMove(Direction::Left));
        QVERIFY(game.move(Direction::Right));
        QCOMPARE(game.at(2).value, top);
        QCOMPARE(game.at(3).value, top);
        QCOMPARE(game.lastTurn().gained, 0);
        QCOMPARE(Game::fromJson(game.toJson()).highest(), top);
    }

    void randomPlayInvariants() {
        std::mt19937 random(8);
        for (int round = 0; round < 24; ++round) {
            Game game(3 + round % 3, static_cast<std::uint64_t>(round));
            while (true) {
                const bool movable = std::any_of(std::begin(directions), std::end(directions),
                                                 [&](Direction direction) { return game.canMove(direction); });
                QCOMPARE(game.over(), !movable);
                const auto before = game.cells();
                const int previousMaximum = std::max_element(before.begin(), before.end(), [](const Tile &a, const Tile &b) { return a.id < b.id; })->id;
                if (!randomMove(game, random))
                    break;
                const auto &turn = game.lastTurn();
                QCOMPARE(static_cast<int>(turn.motions.size()), static_cast<int>(std::count_if(before.begin(), before.end(), [](const Tile &tile) { return tile.value; })));
                for (const auto &motion : turn.motions) {
                    QCOMPARE(before[motion.from].id, motion.id);
                    QCOMPARE(before[motion.from].value, motion.value);
                    const bool merged = std::find(turn.merged.begin(), turn.merged.end(), motion.to) != turn.merged.end();
                    QCOMPARE(motion.consumed, merged);
                    QCOMPARE(game.at(motion.to).value, motion.value * (merged ? 2 : 1));
                    if (!merged)
                        QCOMPARE(game.at(motion.to).id, motion.id);
                }
                for (int cell : turn.merged) {
                    QVERIFY(game.at(cell).id > previousMaximum);
                    QCOMPARE(std::count_if(turn.motions.begin(), turn.motions.end(), [cell](const Motion &motion) { return motion.to == cell; }), std::ptrdiff_t(2));
                }
                QVERIFY(game.at(turn.spawned).id > previousMaximum);
                QVERIFY(uniqueIds(game));
            }
            QVERIFY(game.over());
            QVERIFY(game.moves() > 0);
        }
    }

    void motions() {
        Game game(4, 17);
        load(game, {0, 0, 0, 0, 2, 0, 2, 4, 0, 0, 0, 0, 8, 0, 0, 0});
        const int first = game.at(1, 0).id, second = game.at(1, 2).id, third = game.at(1, 3).id, still = game.at(3, 0).id;
        QCOMPARE(std::set<int>({first, second, third, still}).size(), std::size_t(4));
        QVERIFY(game.move(Direction::Left));
        const auto &turn = game.lastTurn();
        QCOMPARE(turn.motions.size(), std::size_t(4));
        QCOMPARE(describe(turn.motions, first), QString("2 4->4 consumed"));
        QCOMPARE(describe(turn.motions, second), QString("2 6->4 consumed"));
        QCOMPARE(describe(turn.motions, third), QString("4 7->5"));
        QCOMPARE(describe(turn.motions, still), QString("8 12->12"));
        QCOMPARE(turn.merged, std::vector<int>{4});
        QCOMPARE(turn.gained, 4);
        QCOMPARE(game.at(4).value, 4);
        QVERIFY(game.at(4).id > std::max({first, second, third, still}));
        QCOMPARE(game.at(5).id, third);
        QCOMPARE(game.at(12).id, still);
        QVERIFY(turn.spawned != 4 && turn.spawned != 5 && turn.spawned != 12);
        QVERIFY(uniqueIds(game));
    }

    void undoing() {
        Game game(4, 13);
        load(game, {1024, 1024, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 50);
        const auto before = game.cells();
        QVERIFY(!game.undo());
        QVERIFY(game.move(Direction::Left));
        QVERIFY(game.winPending());
        game.keepPlaying();
        QCOMPARE(game.score(), 50 + 2048);
        QVERIFY(game.undo());
        QVERIFY(game.cells() == before);
        QCOMPARE(game.score(), 50);
        QVERIFY(!game.won());
        QVERIFY(game.continued());
        QVERIFY(!game.winPending());
        QCOMPARE(game.moves(), 1);
        QVERIFY(game.lastTurn().motions.empty());
        QVERIFY(game.lastTurn().merged.empty());
        QCOMPARE(game.lastTurn().spawned, -1);
        QCOMPARE(game.lastTurn().gained, 0);
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());
    }

    void historyCap() {
        Game game(5, 21);
        std::vector<std::vector<Tile>> boards{game.cells()};
        std::vector<int> scores{game.score()};
        std::mt19937 random(3);
        const int total = Game::maximumHistory + 8;
        while (game.moves() < total) {
            QVERIFY(randomMove(game, random));
            boards.push_back(game.cells());
            scores.push_back(game.score());
        }
        QVERIFY(game.undo());
        QVERIFY(game.cells() == boards[total - 1]);
        int undone = 1;
        while (game.undo())
            ++undone;
        QCOMPARE(undone, Game::maximumHistory);
        QVERIFY(game.cells() == boards[total - Game::maximumHistory]);
        QCOMPARE(game.score(), scores[total - Game::maximumHistory]);
        QCOMPARE(game.moves(), total);
    }

    void jsonRoundTrip() {
        Game game(4, 23);
        std::mt19937 random(4);
        for (int step = 0; step < 40; ++step)
            QVERIFY(randomMove(game, random));
        game.keepPlaying();
        const auto data = game.toJson();
        auto keys = data.keys();
        keys.sort();
        QCOMPARE(keys, QStringList({"continued", "history", "moves", "score", "size", "values", "version", "won"}));
        QCOMPARE(data.value("version").toInt(), 1);
        QCOMPARE(data.value("size").toInt(), 4);
        QCOMPARE(data.value("values").toArray().size(), qsizetype(16));
        QCOMPARE(data.value("history").toArray().size(), qsizetype(Game::maximumHistory));
        keys = data.value("history").toArray().first().toObject().keys();
        keys.sort();
        QCOMPARE(keys, QStringList({"score", "values", "won"}));

        const auto text = QJsonDocument(data).toJson(QJsonDocument::Compact);
        auto restored = Game::fromJson(QJsonDocument::fromJson(text).object(), 99);
        QCOMPARE(restored.toJson(), data);
        QCOMPARE(valuesOf(restored), valuesOf(game));
        QCOMPARE(restored.score(), game.score());
        QCOMPARE(restored.moves(), 40);
        QCOMPARE(restored.won(), game.won());
        QVERIFY(restored.continued());
        QVERIFY(restored.lastTurn().motions.empty());
        QCOMPARE(restored.lastTurn().spawned, -1);
        QVERIFY(uniqueIds(restored));
        int depth = 0;
        while (restored.undo()) {
            QVERIFY(game.undo());
            QCOMPARE(valuesOf(restored), valuesOf(game));
            QCOMPARE(restored.score(), game.score());
            QVERIFY(uniqueIds(restored));
            ++depth;
        }
        QCOMPARE(depth, Game::maximumHistory);
        QVERIFY(!game.canUndo());
        QVERIFY(randomMove(restored, random));
        QVERIFY(uniqueIds(restored));
    }

    void jsonRejects_data() {
        QTest::addColumn<QJsonObject>("data");
        Game game(4, 31);
        load(game, {2, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8}, 12);
        game.move(Direction::Left);
        const auto valid = game.toJson();
        const auto with = [&](const QString &key, const QJsonValue &value) {
            auto data = valid;
            data.insert(key, value);
            return data;
        };
        const auto without = [&](const QString &key) {
            auto data = valid;
            data.remove(key);
            return data;
        };
        const auto withValue = [&](int index, const QJsonValue &value) {
            auto values = valid.value("values").toArray();
            values[index] = value;
            return with("values", values);
        };
        const auto entry = valid.value("history").toArray().first().toObject();
        const auto withEntry = [&](const QString &key, const QJsonValue &value) {
            auto changed = entry;
            changed.insert(key, value);
            return with("history", QJsonArray{changed});
        };
        auto shortValues = valid.value("values").toArray();
        shortValues.removeLast();
        auto winningValues = valid.value("values").toArray();
        winningValues[0] = 2048;
        QJsonArray longHistory;
        for (int index = 0; index <= Game::maximumHistory; ++index)
            longHistory.append(entry);
        QCOMPARE(Game::fromJson(valid, 1).toJson(), valid);

        QTest::newRow("bad version") << with("version", 2);
        QTest::newRow("text version") << with("version", "1");
        QTest::newRow("no version") << without("version");
        QTest::newRow("size 2") << with("size", 2);
        QTest::newRow("size 9") << with("size", 9);
        QTest::newRow("size mismatch") << with("size", 3);
        QTest::newRow("fractional size") << with("size", 4.5);
        QTest::newRow("short values") << with("values", shortValues);
        QTest::newRow("values object") << with("values", QJsonObject{});
        QTest::newRow("tile 3") << withValue(0, 3);
        QTest::newRow("tile 1") << withValue(0, 1);
        QTest::newRow("negative tile") << withValue(0, -2);
        QTest::newRow("fractional tile") << withValue(0, 2.5);
        QTest::newRow("huge tile") << withValue(0, 2147483648.0);
        QTest::newRow("text tile") << withValue(0, "2");
        QTest::newRow("null tile") << withValue(0, QJsonValue::Null);
        QTest::newRow("negative score") << with("score", -1);
        QTest::newRow("huge score") << with("score", 2147483648.0);
        QTest::newRow("fractional score") << with("score", 1.5);
        QTest::newRow("negative moves") << with("moves", -1);
        QTest::newRow("no moves") << without("moves");
        QTest::newRow("numeric won") << with("won", 1);
        QTest::newRow("no won") << without("won");
        QTest::newRow("text continued") << with("continued", "no");
        QTest::newRow("unearned win") << with("values", winningValues);
        QTest::newRow("history object") << with("history", QJsonObject{});
        QTest::newRow("no history") << without("history");
        QTest::newRow("long history") << with("history", longHistory);
        QTest::newRow("history number") << with("history", QJsonArray{1});
        QTest::newRow("history short values") << withEntry("values", shortValues);
        QTest::newRow("history negative score") << withEntry("score", -5);
        QTest::newRow("history text won") << withEntry("won", "false");
        QTest::newRow("history unearned win") << withEntry("values", winningValues);
    }

    void jsonRejects() {
        QFETCH(QJsonObject, data);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Game::fromJson(data, 1));
    }

    void construction() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(2, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(9, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(0, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(-4, 1));
        for (int size = Game::minimumSize; size <= Game::maximumSize; ++size) {
            Game game(size, static_cast<std::uint64_t>(size));
            QCOMPARE(game.size(), size);
            QCOMPARE(static_cast<int>(game.cells().size()), size * size);
            QCOMPARE(tileCount(game), 2);
            QVERIFY(uniqueIds(game));
        }

        Game game(4, 77), twin(4, 77);
        QVERIFY(game.cells() == twin.cells());
        std::mt19937 first(5), second(5);
        for (int step = 0; step < 20; ++step) {
            randomMove(game, first);
            randomMove(twin, second);
        }
        QCOMPARE(valuesOf(game), valuesOf(twin));

        const auto before = game.cells();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, game.setValues(std::vector<int>(15)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, game.setValues(std::vector<int>(16, 3)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, game.setValues(std::vector<int>(16, 1)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, game.setValues(std::vector<int>(16, -2)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, game.setValues(std::vector<int>(16), -1));
        QVERIFY(game.cells() == before);
    }

    void spawnDistribution() {
        int fours = 0, total = 0;
        std::array<int, 16> positions{};
        for (std::uint64_t seed = 0; seed < 2000; ++seed) {
            const Game game(4, seed);
            for (int index = 0; index < 16; ++index) {
                const int value = game.at(index).value;
                if (value) {
                    ++total;
                    fours += value == 4;
                    ++positions[index];
                }
            }
        }
        QCOMPARE(total, 4000);
        QVERIFY2(fours > 240 && fours < 560, qPrintable(QString::number(fours)));
        for (int count : positions)
            QVERIFY2(count > 125 && count < 375, qPrintable(QString::number(count)));
    }
};

QTEST_APPLESS_MAIN(GameTests)
#include "game_tests.moc"
