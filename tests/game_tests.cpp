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
        Game game(4, Mode::PowerUps, 7);
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
        Game game(4, Mode::PowerUps, 3);
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
        Game game(4, Mode::PowerUps, 11);
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

        Game small(3, Mode::PowerUps, 12);
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
        Game game(4, Mode::PowerUps, 5);
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
        Game game(4, Mode::PowerUps, 9);
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

        Game fresh(4, Mode::PowerUps, 9);
        load(fresh, {1024, 512, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!fresh.won());
        load(fresh, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4096});
        QVERIFY(fresh.won());
        QVERIFY(fresh.winPending());
        QCOMPARE(fresh.highest(), 4096);
    }

    void gameOver() {
        const QList<int> stuck = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};
        Game game(4, Mode::PowerUps, 2);
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

        Game small(3, Mode::PowerUps, 1);
        load(small, {2, 4, 2, 4, 2, 4, 2, 4, 2});
        QVERIFY(small.over());
    }

    void largestValues() {
        constexpr int top = 1 << 30;
        Game game(4, Mode::PowerUps, 6);
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
            Game game(3 + round % 3, Mode::PowerUps, static_cast<std::uint64_t>(round));
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
        Game game(4, Mode::PowerUps, 17);
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
        Game game(4, Mode::PowerUps, 13);
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
        QCOMPARE(game.moves(), 0);
        QVERIFY(game.lastTurn().motions.empty());
        QVERIFY(game.lastTurn().merged.empty());
        QCOMPARE(game.lastTurn().spawned, -1);
        QCOMPARE(game.lastTurn().gained, 0);
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());
    }

    void historyCap() {
        Game game(5, Mode::PowerUps, 21);
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
        while (true) {
            game.setCharges(Power::Undo, Game::maximumCharges);
            if (!game.undo())
                break;
            ++undone;
        }
        QCOMPARE(undone, Game::maximumHistory);
        QVERIFY(game.cells() == boards[total - Game::maximumHistory]);
        QCOMPARE(game.score(), scores[total - Game::maximumHistory]);
        QCOMPARE(game.moves(), total - Game::maximumHistory);
    }

    void jsonRoundTrip() {
        Game game(4, Mode::PowerUps, 23);
        std::mt19937 random(4);
        for (int step = 0; step < 40; ++step)
            QVERIFY(randomMove(game, random));
        game.keepPlaying();
        const auto data = game.toJson();
        auto keys = data.keys();
        keys.sort();
        QCOMPARE(keys, QStringList({"charges", "continued", "history", "mode", "moves", "score", "size", "values", "version", "won"}));
        QCOMPARE(data.value("mode").toString(), QString("powerups"));
        QCOMPARE(data.value("charges").toArray(), QJsonArray({2, 1, 0}));
        QCOMPARE(data.value("version").toInt(), 1);
        QCOMPARE(data.value("size").toInt(), 4);
        QCOMPARE(data.value("values").toArray().size(), qsizetype(16));
        QCOMPARE(data.value("history").toArray().size(), qsizetype(Game::maximumHistory));
        keys = data.value("history").toArray().first().toObject().keys();
        keys.sort();
        QCOMPARE(keys, QStringList({"charges", "moves", "score", "values", "won"}));

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
        while (true) {
            restored.setCharges(Power::Undo, Game::maximumCharges);
            game.setCharges(Power::Undo, Game::maximumCharges);
            if (!restored.undo())
                break;
            QVERIFY(game.undo());
            QCOMPARE(valuesOf(restored), valuesOf(game));
            QCOMPARE(restored.score(), game.score());
            QCOMPARE(restored.moves(), game.moves());
            QVERIFY(uniqueIds(restored));
            ++depth;
        }
        QCOMPARE(depth, Game::maximumHistory);
        QCOMPARE(restored.moves(), 40 - Game::maximumHistory);
        QVERIFY(!game.canUndo());
        QVERIFY(randomMove(restored, random));
        QVERIFY(uniqueIds(restored));
    }

    void jsonRejects_data() {
        QTest::addColumn<QJsonObject>("data");
        Game game(4, Mode::PowerUps, 31);
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
        QTest::newRow("size 6") << with("size", 6);
        QTest::newRow("size 9") << with("size", 9);
        QTest::newRow("mode number") << with("mode", 1);
        QTest::newRow("mode unknown") << with("mode", "sprint");
        QTest::newRow("mode null") << with("mode", QJsonValue::Null);
        QTest::newRow("charges object") << with("charges", QJsonObject{});
        QTest::newRow("charges number") << with("charges", 2);
        QTest::newRow("charges short") << with("charges", QJsonArray{2, 2});
        QTest::newRow("charges long") << with("charges", QJsonArray{2, 2, 2, 2});
        QTest::newRow("charges too high") << with("charges", QJsonArray{3, 2, 2});
        QTest::newRow("charges negative") << with("charges", QJsonArray{-1, 2, 2});
        QTest::newRow("charges fractional") << with("charges", QJsonArray{1.5, 2, 2});
        QTest::newRow("charges text") << with("charges", QJsonArray{"2", 2, 2});
        QTest::newRow("charges null") << with("charges", QJsonArray{QJsonValue::Null, 2, 2});
        QTest::newRow("history charges too high") << withEntry("charges", QJsonArray{2, 2, 3});
        QTest::newRow("history charges short") << withEntry("charges", QJsonArray{2});
        QTest::newRow("classic with history") << with("mode", "classic");
        QTest::newRow("casual with charges") << with("mode", "casual");
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
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(2, Mode::PowerUps, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(6, Mode::PowerUps, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(9, Mode::PowerUps, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(0, Mode::PowerUps, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Game(-4, Mode::PowerUps, 1));
        for (int size = Game::minimumSize; size <= Game::maximumSize; ++size) {
            Game game(size, Mode::PowerUps, static_cast<std::uint64_t>(size));
            QCOMPARE(game.size(), size);
            QCOMPARE(static_cast<int>(game.cells().size()), size * size);
            QCOMPARE(tileCount(game), 2);
            QVERIFY(uniqueIds(game));
        }

        Game game(4, Mode::PowerUps, 77), twin(4, Mode::PowerUps, 77);
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

    void startingCharges() {
        Game game(4, Mode::PowerUps, 63);
        QCOMPARE(Game::powerCount, 3);
        QCOMPARE(Game::startingCharges(Power::Undo), 2);
        QCOMPARE(Game::startingCharges(Power::Swap), 1);
        QCOMPARE(Game::startingCharges(Power::Remove), 0);
        QCOMPARE(Game::maximumCharges, 2);
        QCOMPARE(Game::maximumSize, 5);
        QCOMPARE(Game::rewardValue(Power::Undo), 128);
        QCOMPARE(Game::rewardValue(Power::Swap), 256);
        QCOMPARE(Game::rewardValue(Power::Remove), 512);
        QVERIFY(game.mode() == Mode::PowerUps);
        for (auto power : {Power::Undo, Power::Swap, Power::Remove})
            QCOMPARE(game.charges(power), Game::startingCharges(power));
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());

        game.setCharges(Power::Swap, 1);
        load(game, {2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QCOMPARE(game.charges(Power::Swap), 1);
        game.setCharges(Power::Undo, 7);
        QCOMPARE(game.charges(Power::Undo), Game::maximumCharges);
        game.setCharges(Power::Undo, -3);
        QCOMPARE(game.charges(Power::Undo), 0);
    }

    void undoCharges() {
        Game game(4, Mode::PowerUps, 41);
        load(game, {2, 0, 0, 0, 4, 0, 0, 0, 8, 0, 0, 0, 16, 0, 0, 0});
        QVERIFY(game.move(Direction::Right));
        QVERIFY(game.move(Direction::Left));
        QVERIFY(game.move(Direction::Right));
        QVERIFY(game.undo());
        QCOMPARE(game.charges(Power::Undo), 1);
        const auto before = game.cells();
        QVERIFY(game.undo());
        QCOMPARE(game.charges(Power::Undo), 0);
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());
        QVERIFY(game.cells() != before);
        QCOMPARE(game.moves(), 1);
    }

    void undoRestoresMoves() {
        Game game(4, Mode::Casual, 43);
        load(game, {2, 0, 0, 0, 4, 0, 0, 0, 8, 0, 0, 0, 16, 0, 0, 0});
        QVERIFY(game.move(Direction::Right));
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.moves(), 2);
        int first = -1, second = -1;
        for (int index = 0; index < 16; ++index) {
            if (game.at(index).value && first < 0)
                first = index;
            else if (game.at(index).value && game.at(index).value != game.at(first).value && second < 0)
                second = index;
        }
        QVERIFY(game.swap(first, second));
        QCOMPARE(game.moves(), 2);
        QVERIFY(game.undo());
        QCOMPARE(game.moves(), 2);
        QVERIFY(game.undo());
        QCOMPARE(game.moves(), 1);
        QVERIFY(game.undo());
        QCOMPARE(game.moves(), 0);
        QVERIFY(!game.undo());
    }

    void jsonLegacyHistoryMoves() {
        Game game(4, Mode::PowerUps, 45);
        load(game, {2, 0, 0, 0, 4, 0, 0, 0, 8, 0, 0, 0, 16, 0, 0, 0});
        QVERIFY(game.move(Direction::Right));
        QVERIFY(game.move(Direction::Left));
        auto data = game.toJson();
        QJsonArray history;
        for (const auto &entry : data.value("history").toArray()) {
            auto snapshot = entry.toObject();
            snapshot.remove("moves");
            history.append(snapshot);
        }
        data.insert("history", history);
        // Saves from before undo restored the move count keep the count unchanged on undo.
        auto restored = Game::fromJson(data, 7);
        QVERIFY(restored.undo());
        QCOMPARE(restored.moves(), 2);
        data.insert("history", QJsonArray{QJsonObject{{"values", data.value("values")}, {"score", 0}, {"moves", -1},
                                                      {"won", false}, {"charges", QJsonArray{2, 1, 0}}}});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Game::fromJson(data, 7));
    }

    void undoNeedsCharge() {
        Game game(4, Mode::PowerUps, 47);
        load(game, {2, 0, 0, 0, 4, 0, 0, 0, 8, 0, 0, 0, 16, 0, 0, 0});
        QVERIFY(game.move(Direction::Right));
        const auto after = game.cells();
        QVERIFY(game.canUndo());
        game.setCharges(Power::Undo, 0);
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());
        QVERIFY(game.cells() == after);
        QCOMPARE(game.moves(), 1);
    }

    void undoRevokesEarned() {
        Game game(4, Mode::PowerUps, 49);
        game.setCharges(Power::Undo, 1);
        load(game, {64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.lastTurn().earned[0], 1);
        QCOMPARE(game.charges(Power::Undo), 2);
        QVERIFY(game.undo());
        QCOMPARE(game.charges(Power::Undo), 0);
        QCOMPARE(game.lastTurn().earned[0], 0);
        QVERIFY(!game.canUndo());
    }

    void rewardsEarned() {
        struct Case {
            Power power;
            int value;
        };
        for (const auto &entry : {Case{Power::Undo, 128}, Case{Power::Swap, 256}, Case{Power::Remove, 512}}) {
            const int index = static_cast<int>(entry.power);
            Game game(4, Mode::PowerUps, 50 + index);
            game.setCharges(entry.power, 1);
            QList<int> row(16, 0);
            row[0] = row[1] = entry.value / 2;
            load(game, row);
            QVERIFY(game.move(Direction::Left));
            QCOMPARE(game.charges(entry.power), 2);
            for (int other = 0; other < Game::powerCount; ++other)
                QCOMPARE(game.lastTurn().earned[other], other == index ? 1 : 0);
        }
    }

    void earningCapped() {
        Game game(4, Mode::PowerUps, 51);
        load(game, {64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.charges(Power::Undo), 2);
        QCOMPARE(game.lastTurn().earned[0], 0);

        game.setCharges(Power::Undo, 1);
        load(game, {64, 64, 64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.charges(Power::Undo), 2);
        QCOMPARE(game.lastTurn().earned[0], 1);
    }

    void swapRules() {
        Game game(4, Mode::PowerUps, 53);
        load(game, {2, 2, 0, 4, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!game.canSwap(0, 0));
        QVERIFY(!game.canSwap(0, 1));
        QVERIFY(!game.canSwap(0, 2));
        QVERIFY(!game.canSwap(2, 5));
        QVERIFY(!game.canSwap(-1, 3));
        QVERIFY(!game.canSwap(3, 16));
        QVERIFY(game.canSwap(1, 3));
        QVERIFY(!game.swap(0, 1));
        QVERIFY(!game.canUndo());

        game.setCharges(Power::Swap, 0);
        QVERIFY(!game.canSwap(1, 3));
        QVERIFY(!game.swap(1, 3));
    }

    void swapTiles() {
        Game game(4, Mode::PowerUps, 55);
        load(game, {2, 4, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 10);
        const auto before = game.cells();
        QVERIFY(game.swap(0, 3));
        QCOMPARE(game.at(0).value, 8);
        QCOMPARE(game.at(3).value, 2);
        QCOMPARE(game.at(0).id, before[3].id);
        QCOMPARE(game.at(3).id, before[0].id);
        QCOMPARE(game.at(1).id, before[1].id);
        QCOMPARE(game.score(), 10);
        QCOMPARE(game.moves(), 0);
        QCOMPARE(game.charges(Power::Swap), 0);
        QCOMPARE(game.lastTurn().spawned, -1);
        QCOMPARE(game.lastTurn().motions.size(), std::size_t(3));
        QCOMPARE(describe(game.lastTurn().motions, before[0].id), QString("2 0->3"));
        QCOMPARE(describe(game.lastTurn().motions, before[3].id), QString("8 3->0"));
        QCOMPARE(describe(game.lastTurn().motions, before[1].id), QString("4 1->1"));
        QVERIFY(uniqueIds(game));
        QVERIFY(game.canUndo());

        QVERIFY(game.undo());
        QVERIFY(game.cells() == before);
        QCOMPARE(game.charges(Power::Swap), 0);
        QCOMPARE(game.charges(Power::Undo), 1);
    }

    void removeRules() {
        Game game(4, Mode::PowerUps, 57);
        game.setCharges(Power::Remove, 1);
        load(game, {512, 512, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(!game.canRemove(0));
        QVERIFY(!game.canRemove(4));
        QVERIFY(game.canRemove(512));
        QVERIFY(!game.remove(256));
        game.setCharges(Power::Remove, 0);
        QVERIFY(!game.canRemove(512));
        QVERIFY(!game.remove(512));
    }

    void removeTiles() {
        Game game(4, Mode::PowerUps, 59);
        game.setCharges(Power::Remove, 2);
        load(game, {512, 2, 512, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 30);
        const auto before = game.cells();
        QVERIFY(game.remove(512));
        QList<int> expected(16, 0);
        expected[1] = 2;
        expected[3] = 4;
        QCOMPARE(valuesOf(game), expected);
        QCOMPARE(game.lastTurn().spawned, -1);
        QCOMPARE(game.lastTurn().motions.size(), std::size_t(2));
        QCOMPARE(describe(game.lastTurn().motions, before[1].id), QString("2 1->1"));
        QCOMPARE(describe(game.lastTurn().motions, before[3].id), QString("4 3->3"));
        QCOMPARE(game.score(), 30);
        QCOMPARE(game.moves(), 0);
        QCOMPARE(game.charges(Power::Remove), 1);
        QVERIFY(uniqueIds(game));

        QVERIFY(game.undo());
        QVERIFY(game.cells() == before);
        QCOMPARE(game.charges(Power::Remove), 1);
    }

    void removeEmptiesBoard() {
        Game game(4, Mode::PowerUps, 61);
        game.setCharges(Power::Remove, 1);
        load(game, {512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
        QVERIFY(game.remove(512));
        QCOMPARE(tileCount(game), 1);
        const int spawned = game.lastTurn().spawned;
        QVERIFY(spawned >= 0);
        QVERIFY(game.at(spawned).value == 2 || game.at(spawned).value == 4);
        QVERIFY(game.lastTurn().motions.empty());
        QCOMPARE(game.moves(), 0);
        QCOMPARE(game.score(), 0);
    }

    void jsonCharges() {
        Game game(4, Mode::PowerUps, 65);
        load(game, {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Left));
        game.setCharges(Power::Swap, 1);
        game.setCharges(Power::Remove, 0);
        const auto data = game.toJson();
        QCOMPARE(data.value("charges").toArray(), QJsonArray({2, 1, 0}));
        QCOMPARE(data.value("history").toArray().first().toObject().value("charges").toArray(), QJsonArray({2, 1, 0}));

        auto restored = Game::fromJson(data, 3);
        QCOMPARE(restored.charges(Power::Undo), 2);
        QCOMPARE(restored.charges(Power::Swap), 1);
        QCOMPARE(restored.charges(Power::Remove), 0);
        QVERIFY(restored.undo());
        QCOMPARE(restored.charges(Power::Undo), 1);
        QCOMPARE(restored.charges(Power::Swap), 1);
        QCOMPARE(restored.charges(Power::Remove), 0);

        auto legacy = data;
        legacy.remove("mode");
        legacy.remove("charges");
        auto history = legacy.value("history").toArray();
        for (int index = 0; index < history.size(); ++index) {
            auto entry = history[index].toObject();
            entry.remove("charges");
            history[index] = entry;
        }
        legacy.insert("history", history);
        const auto old = Game::fromJson(legacy, 4);
        QVERIFY(old.mode() == Mode::PowerUps);
        for (auto power : {Power::Undo, Power::Swap, Power::Remove})
            QCOMPARE(old.charges(power), Game::startingCharges(power));
        QVERIFY(old.canUndo());
    }

    void classic() {
        Game game(4, Mode::Classic, 67);
        QVERIFY(game.mode() == Mode::Classic);
        for (auto power : {Power::Undo, Power::Swap, Power::Remove})
            QCOMPARE(game.charges(power), 0);
        game.setCharges(Power::Undo, 2);
        QCOMPARE(game.charges(Power::Undo), 0);

        load(game, {64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        QVERIFY(game.move(Direction::Left));
        QCOMPARE(game.lastTurn().earned[0], 0);
        QCOMPARE(game.charges(Power::Undo), 0);
        QVERIFY(!game.canUndo());
        QVERIFY(!game.undo());
        QVERIFY(!game.canSwap(0, 1));
        QVERIFY(!game.canRemove(128));
        QVERIFY(game.toJson().value("history").toArray().isEmpty());

        std::mt19937 random(6);
        for (int step = 0; step < 10; ++step)
            QVERIFY(randomMove(game, random));
        const auto data = game.toJson();
        QCOMPARE(data.value("mode").toString(), QString("classic"));
        QCOMPARE(data.value("charges").toArray(), QJsonArray({0, 0, 0}));
        QVERIFY(data.value("history").toArray().isEmpty());
        const auto restored = Game::fromJson(data, 3);
        QVERIFY(restored.mode() == Mode::Classic);
        QCOMPARE(restored.toJson(), data);

        auto stored = data;
        stored.insert("charges", QJsonArray{2, 2, 2});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Game::fromJson(stored, 1));
    }

    void casual() {
        Game game(4, Mode::Casual, 67);
        QVERIFY(game.mode() == Mode::Casual);
        QVERIFY(game.unlimitedPowers());
        QVERIFY(game.hasPowers());
        QVERIFY(!Game(4, Mode::Classic, 1).unlimitedPowers());
        QVERIFY(!Game(4, Mode::Classic, 1).hasPowers());
        QVERIFY(!Game(4, Mode::PowerUps, 1).unlimitedPowers());
        QVERIFY(Game(4, Mode::PowerUps, 1).hasPowers());
        for (auto power : {Power::Undo, Power::Swap, Power::Remove})
            QCOMPARE(game.charges(power), 0);
        game.setCharges(Power::Undo, 2);
        QCOMPARE(game.charges(Power::Undo), 0);

        // Nothing to undo before the first move, then undo needs no charge, however often it is used.
        QVERIFY(!game.canUndo());
        load(game, {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        for (int round = 0; round < 5; ++round) {
            QVERIFY(game.move(Direction::Left) || game.move(Direction::Right));
            QVERIFY(game.canUndo());
            QVERIFY(game.undo());
            QCOMPARE(game.score(), 0);
            QCOMPARE(game.charges(Power::Undo), 0);
            QVERIFY(!game.canUndo());
        }
        QVERIFY(game.move(Direction::Left));
        QVERIFY(game.move(Direction::Down));
        QVERIFY(game.undo());
        QVERIFY(game.undo());
        QVERIFY(!game.undo());

        // Swap and remove work repeatedly without charges.
        load(game, {2, 4, 8, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        for (int round = 0; round < 4; ++round) {
            QVERIFY(game.canSwap(0, 1));
            QVERIFY(game.swap(0, 1));
            QCOMPARE(game.charges(Power::Swap), 0);
        }
        QVERIFY(!game.canSwap(0, 4));
        QVERIFY(!game.canSwap(0, 0));
        QVERIFY(!game.swap(0, 4));
        QVERIFY(!game.canRemove(32));
        QVERIFY(game.canRemove(8));
        QVERIFY(game.remove(8));
        QVERIFY(!game.canRemove(8));
        QVERIFY(game.remove(4));
        QVERIFY(game.remove(2));
        QCOMPARE(game.charges(Power::Remove), 0);
        QVERIFY(game.canUndo());
        for (int step = 0; step < 7; ++step)
            QVERIFY(game.undo());
        QVERIFY(!game.canUndo());

        // Making reward tiles earns nothing.
        for (int value : {128, 256, 512}) {
            load(game, {value / 2, value / 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
            QVERIFY(game.move(Direction::Left));
            QCOMPARE(game.highest(), value);
            for (int index = 0; index < Game::powerCount; ++index)
                QCOMPARE(game.lastTurn().earned[index], 0);
            for (auto power : {Power::Undo, Power::Swap, Power::Remove})
                QCOMPARE(game.charges(power), 0);
            QVERIFY(game.undo());
        }

        // History is capped like Standard mode.
        std::mt19937 random(6);
        for (int step = 0; step < 100 && randomMove(game, random); ++step) {
        }
        QVERIFY(game.toJson().value("history").toArray().size() <= Game::maximumHistory);

        QVERIFY(game.undo()); // Random play may have ended the game; undo makes room for another move.
        QVERIFY(randomMove(game, random));
        const auto data = game.toJson();
        QCOMPARE(data.value("mode").toString(), QString("casual"));
        QCOMPARE(data.value("charges").toArray(), QJsonArray({0, 0, 0}));
        QVERIFY(!data.value("history").toArray().isEmpty());
        auto restored = Game::fromJson(data, 3);
        QVERIFY(restored.mode() == Mode::Casual);
        QVERIFY(restored.unlimitedPowers());
        QCOMPARE(restored.toJson(), data);
        QVERIFY(restored.canUndo());
        QVERIFY(restored.undo());

        auto stored = data;
        stored.insert("charges", QJsonArray{2, 2, 2});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Game::fromJson(stored, 1));
    }

    void spawnDistribution() {
        int fours = 0, total = 0;
        std::array<int, 16> positions{};
        for (std::uint64_t seed = 0; seed < 2000; ++seed) {
            const Game game(4, Mode::PowerUps, seed);
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
