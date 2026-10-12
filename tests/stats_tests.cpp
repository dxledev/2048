/**
 * @file stats_tests.cpp
 * @brief Statistics recording, aggregation, streaks, play time, and persistence tests.
 */
#include "core/stats.h"
#include "core/storage.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>
#include <random>

using namespace g2048;

namespace {

constexpr Direction directions[] = {Direction::Left, Direction::Right, Direction::Up, Direction::Down};
/** @brief A full 4×4 board with no equal neighbours: no move is possible. */
const std::vector<int> stuck = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};

std::vector<int> cells(std::initializer_list<int> head, int size = 4) {
    std::vector<int> values(head);
    values.resize(size * size, 0);
    return values;
}

/** @brief Move and record it, failing the test when the move does nothing. */
void step(Stats &stats, Game &game, Direction direction, qint64 now) {
    QVERIFY(game.move(direction));
    stats.recordMove(game, direction, now);
}

/** @brief Start a game on @p size, make one merging move, and leave it live. */
void playOne(Stats &stats, Game &game, int size, qint64 now) {
    game = Game(size, Mode::PowerUps, 3);
    stats.begin(game, now);
    game.setValues(cells({2, 2}, size));
    step(stats, game, Direction::Left, now + 1000);
}

/** @brief Live game that reaches 2048 on its first move. */
void winOne(Stats &stats, Game &game, qint64 now) {
    game = Game(4, Mode::PowerUps, 5);
    stats.begin(game, now);
    game.setValues(cells({1024, 1024}), 1000);
    step(stats, game, Direction::Left, now + 1000);
}

/** @brief Live game that is stuck after one move. */
void loseOne(Stats &stats, Game &game, qint64 now) {
    playOne(stats, game, 4, now);
    game.setValues(stuck, 300);
    stats.attach(game, now + 2000);
}

bool randomMove(Stats &stats, Game &game, std::mt19937 &random, qint64 now) {
    std::vector<Direction> options;
    for (auto direction : directions) {
        if (game.canMove(direction))
            options.push_back(direction);
    }
    if (options.empty())
        return false;
    const auto direction = options[std::uniform_int_distribution<std::size_t>(0, options.size() - 1)(random)];
    game.move(direction);
    stats.recordMove(game, direction, now);
    return true;
}

/** @brief Several finished games on different sizes plus a live one, with undos sprinkled in. */
Stats played() {
    Stats stats;
    std::mt19937 random(11);
    qint64 now = 1'700'000'000'000;
    for (const int size : {4, 3, 5, 4, 3}) {
        Game game(size, Mode::PowerUps, size * 17);
        stats.begin(game, now);
        for (int move = 0; move < 60 && randomMove(stats, game, random, now += 1500); ++move) {
            if (move % 13 == 12) {
                game.setCharges(Power::Undo, Game::maximumCharges);
                if (game.undo())
                    stats.recordUndo(game, now += 700);
            }
        }
    }
    return stats;
}

}

class StatsTests : public QObject {
    Q_OBJECT
private slots:
    void freshGameHasNoTotals() {
        Stats stats;
        const Game game(4, Mode::PowerUps, 1);
        stats.begin(game, 1000);
        QCOMPARE(stats.since(), 1000);
        QCOMPARE(stats.current().started, 1000);
        QCOMPARE(stats.current().size, 4);
        QCOMPARE(stats.totals().games, 0);
        QCOMPARE(stats.spawnedTwos() + stats.spawnedFours(), 2);
        QCOMPARE(stats.currentStreak(), 0);
    }

    void moveUpdatesLiveGame() {
        Stats stats;
        Game game(4, Mode::PowerUps, 2);
        stats.begin(game, 0);
        game.setValues(cells({2, 2, 4, 4}));
        const auto spawned = stats.spawnedTwos() + stats.spawnedFours();
        step(stats, game, Direction::Left, 2000);
        const auto &record = stats.current();
        QCOMPARE(record.moves, 1);
        QCOMPARE(record.merges, 2);
        QCOMPARE(record.score, 12);
        QCOMPARE(record.highest, 8);
        QCOMPARE(stats.directions()[int(Direction::Left)], 1);
        QCOMPARE(stats.spawnedTwos() + stats.spawnedFours(), spawned + 1);
        const auto totals = stats.totals();
        QCOMPARE(totals.games, 1);
        QCOMPARE(totals.moves, 1);
        QCOMPARE(totals.merges, 2);
        QCOMPARE(totals.bestScore, 12);
        QCOMPARE(totals.bestTile, 8);
        QCOMPARE(stats.recent().size(), std::size_t(0));
    }

    void zeroMoveGamesAreNotCounted() {
        Stats stats;
        stats.begin(Game(4, Mode::PowerUps, 1), 5);
        stats.begin(Game(5, Mode::PowerUps, 1), 10);
        stats.begin(Game(4, Mode::PowerUps, 1), 20);
        QCOMPARE(stats.recent().size(), std::size_t(0));
        QCOMPARE(stats.totals().games, 0);
        QCOMPARE(stats.since(), 5);
        QCOMPARE(stats.current().started, 20);
    }

    void newGameFinishesTheLiveOne() {
        Stats stats;
        Game game;
        playOne(stats, game, 4, 100);
        const auto live = stats.current();
        stats.begin(Game(4, Mode::PowerUps, 9), 5000);
        QCOMPARE(stats.recent().size(), std::size_t(1));
        QCOMPARE(stats.recent().back(), live);
        QCOMPARE(stats.current().moves, 0);
        QCOMPARE(stats.current().started, 5000);
        QCOMPARE(stats.totals(4).games, 1);
        QCOMPARE(stats.totals(4).score, 4);
        QCOMPARE(stats.totals(4).losses, 0);
    }

    void winsAndFastestWin() {
        Stats stats;
        Game game;
        winOne(stats, game, 0);
        QVERIFY(stats.current().won());
        QCOMPARE(stats.current().wonAtMove, 1);
        auto totals = stats.totals();
        QCOMPARE(totals.wins, 1);
        QCOMPARE(totals.fastestWin, 1);
        QCOMPARE(totals.reached(2048), 1);
        QCOMPARE(totals.winRate(), 1.0);
        QCOMPARE(stats.currentStreak(), 1);

        game = Game(4, Mode::PowerUps, 6);
        stats.begin(game, 5000);
        game.setValues(cells({512, 512, 1024}));
        step(stats, game, Direction::Left, 6000);
        step(stats, game, Direction::Left, 7000);
        QCOMPARE(stats.current().wonAtMove, 2);
        totals = stats.totals();
        QCOMPARE(totals.wins, 2);
        QCOMPARE(totals.fastestWin, 1);
        QCOMPARE(stats.currentStreak(), 2);
    }

    void lossesAndStreaks() {
        Stats stats;
        Game game;
        winOne(stats, game, 0);
        winOne(stats, game, 10'000);
        QCOMPARE(stats.currentStreak(), 2);
        loseOne(stats, game, 20'000);
        QVERIFY(stats.current().over);
        QCOMPARE(stats.currentStreak(), 0);
        QCOMPARE(stats.bestStreak(), 2);
        QCOMPARE(stats.totals().losses, 1);
        QCOMPARE(stats.totals().winRate(), 2.0 / 3.0);

        // Undoing out of the loss makes the game unfinished again, which keeps the streak.
        game.setValues(cells({2, 2}));
        stats.recordUndo(game, 30'000);
        QVERIFY(!stats.current().over);
        QCOMPARE(stats.currentStreak(), 2);

        loseOne(stats, game, 40'000);
        stats.begin(Game(4, Mode::PowerUps, 1), 50'000);
        QCOMPARE(stats.currentStreak(), 0);
        QCOMPARE(stats.bestStreak(), 2);
        QCOMPARE(stats.totals().losses, 1);
        winOne(stats, game, 60'000);
        QCOMPARE(stats.currentStreak(), 1);
        QCOMPARE(stats.bestStreak(), 2);
    }

    void undoKeepsPeaks() {
        Stats stats;
        Game game;
        winOne(stats, game, 0);
        QVERIFY(game.undo());
        stats.recordUndo(game, 3000);
        const auto &record = stats.current();
        QCOMPARE(game.highest(), 1024);
        QCOMPARE(record.highest, 2048);
        QCOMPARE(record.wonAtMove, 1);
        QCOMPARE(record.undos, 1);
        QCOMPARE(record.score, 1000);
        QCOMPARE(record.moves, 1);
        QCOMPARE(stats.totals().undos, 1);
        QCOMPARE(stats.totals().score, 1000);
    }

    void idleTimeIsCapped() {
        Stats stats;
        Game game(4, Mode::PowerUps, 1);
        stats.begin(game, 1000);
        game.setValues(cells({2, 0, 0, 0, 4}));
        step(stats, game, Direction::Right, 6000);
        QCOMPARE(stats.current().played, 5000);
        step(stats, game, Direction::Left, 6000 + 10 * 60'000);
        QCOMPARE(stats.current().played, 5000 + Stats::idleLimit);
        // A clock that jumped backwards adds nothing.
        step(stats, game, Direction::Right, 1000);
        QCOMPARE(stats.current().played, 5000 + Stats::idleLimit);
        QCOMPARE(stats.current().lastActive, 1000);
        QCOMPARE(stats.totals().played, 5000 + Stats::idleLimit);
    }

    void suspendStopsTheClock() {
        Stats stats;
        Game game(4, Mode::PowerUps, 1);
        stats.begin(game, 1000);
        game.setValues(cells({2, 0, 0, 0, 4}));
        stats.suspend();
        QCOMPARE(stats.current().lastActive, 0);
        step(stats, game, Direction::Right, 30'000);
        QCOMPARE(stats.current().played, 0);
        step(stats, game, Direction::Left, 32'000);
        QCOMPARE(stats.current().played, 2000);
    }

    void undoneMovesStillCount() {
        Stats stats;
        Game game(4, Mode::Casual, 9);
        stats.begin(game, 100);
        game.setValues(cells({2, 2, 4}));
        step(stats, game, Direction::Left, 1000);
        QVERIFY(game.undo());
        stats.recordUndo(game, 2000);
        QCOMPARE(game.moves(), 0);
        step(stats, game, Direction::Left, 3000);
        QCOMPARE(game.moves(), 1);
        QCOMPARE(stats.current().moves, 2);
        const auto started = stats.current().started;
        stats.attach(game, 4000);
        QCOMPARE(stats.current().started, started);
        QCOMPARE(stats.recent().size(), std::size_t(0));
    }

    void attachKeepsOrReplaces() {
        Stats stats;
        Game game;
        playOne(stats, game, 4, 100);
        const auto started = stats.current().started;
        const auto played = stats.current().played;
        stats.attach(game, 99'000);
        QCOMPARE(stats.current().started, started);
        QCOMPARE(stats.current().played, played);
        QCOMPARE(stats.current().lastActive, 0);
        QCOMPARE(stats.recent().size(), std::size_t(0));

        const Game other(5, Mode::PowerUps, 4);
        stats.attach(other, 120'000);
        QCOMPARE(stats.recent().size(), std::size_t(1));
        QCOMPARE(stats.current().size, 5);
        QCOMPARE(stats.current().started, 120'000);

        Stats fresh;
        fresh.attach(other, 7);
        QCOMPARE(fresh.since(), 7);
        QCOMPARE(fresh.current().size, 5);
        QCOMPARE(fresh.spawnedTwos() + fresh.spawnedFours(), 2);
    }

    void recentGamesAreCapped() {
        Stats stats;
        Game game;
        for (int index = 0; index < 35; ++index)
            playOne(stats, game, 4, index * 10'000);
        stats.begin(Game(4, Mode::PowerUps, 1), 1'000'000);
        QCOMPARE(int(stats.recent().size()), Stats::maximumRecent);
        QCOMPARE(stats.recent().front().started, 50'000);
        QCOMPARE(stats.recent().back().started, 340'000);
        QCOMPARE(stats.totals().games, 35);
        QCOMPARE(stats.totals().moves, 35);
    }

    void totalsPerSize() {
        Stats stats;
        Game game;
        playOne(stats, game, 3, 0);
        winOne(stats, game, 10'000);
        playOne(stats, game, 5, 20'000);
        QCOMPARE(stats.totals(3).games, 1);
        QCOMPARE(stats.totals(4).games, 1);
        QCOMPARE(stats.totals(5).games, 1);
        QCOMPARE(stats.totals(6).games, 0);
        QCOMPARE(stats.totals(4).bestScore, 3048);
        QCOMPARE(stats.totals(5).bestScore, 4);
        const auto all = stats.totals();
        QCOMPARE(all.games, 3);
        QCOMPARE(all.wins, 1);
        QCOMPARE(all.bestScore, 3048);
        QCOMPARE(all.bestTile, 2048);
        QCOMPARE(all.score, 3056);
        QCOMPARE(all.averageScore(), 3056.0 / 3);
        QCOMPARE(all.peaks.at(4), 2);
        QCOMPARE(all.peaks.at(2048), 1);
        QCOMPARE(stats.totals(6).averageScore(), 0.0);
        QCOMPARE(stats.totals(6).winRate(), 0.0);
    }

    void reachedCountsAtLeast() {
        Totals totals;
        totals.peaks = {{128, 2}, {256, 1}, {2048, 1}};
        QCOMPARE(totals.reached(2), 4);
        QCOMPARE(totals.reached(128), 4);
        QCOMPARE(totals.reached(256), 2);
        QCOMPARE(totals.reached(512), 1);
        QCOMPARE(totals.reached(2048), 1);
        QCOMPARE(totals.reached(4096), 0);
    }

    void directionsAndSpawns() {
        Stats stats;
        Game game(4, Mode::PowerUps, 21);
        stats.begin(game, 1);
        long long tiles = stats.spawnedTwos() + stats.spawnedFours();
        std::array<long long, 4> expected{};
        for (int move = 0; move < 200; ++move) {
            const auto direction = directions[move % 4];
            if (!game.move(direction))
                continue;
            stats.recordMove(game, direction, move);
            ++expected[int(direction)];
            tiles += game.lastTurn().spawned >= 0;
        }
        QCOMPARE(stats.directions(), expected);
        QCOMPARE(stats.directions()[0] + stats.directions()[1] + stats.directions()[2] + stats.directions()[3],
                 stats.current().moves);
        QCOMPARE(stats.spawnedTwos() + stats.spawnedFours(), tiles);
        QVERIFY(stats.spawnedTwos() > stats.spawnedFours());
    }

    void resetForgetsEverything() {
        Stats stats = played();
        QVERIFY(stats.totals().games > 0);
        const Game game(5, Mode::PowerUps, 2);
        stats.reset(game, 42);
        QCOMPARE(stats.totals().games, 0);
        QCOMPARE(stats.recent().size(), std::size_t(0));
        QCOMPARE(stats.since(), 42);
        QCOMPARE(stats.current().size, 5);
        QCOMPARE(stats.bestStreak(), 0);
        QCOMPARE(stats.spawnedTwos() + stats.spawnedFours(), 2);
        for (const auto count : stats.directions())
            QCOMPARE(count, 0);
    }

    void jsonRoundTrip() {
        const Stats stats = played();
        QVERIFY(stats.recent().size() >= 4);
        const auto json = stats.toJson();
        const auto restored = Stats::fromJson(json);
        QCOMPARE(restored.toJson(), json);
        QCOMPARE(restored.current(), stats.current());
        QVERIFY(restored.recent() == stats.recent());
        QCOMPARE(restored.totals().games, stats.totals().games);
        QCOMPARE(restored.totals().played, stats.totals().played);
        QCOMPARE(restored.totals(4).peaks, stats.totals(4).peaks);
        QCOMPARE(restored.directions(), stats.directions());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("nested/stats.json");
        saveStats(path, stats);
        QCOMPARE(loadStats(path).toJson(), json);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadStats(directory.filePath("missing.json")));
    }

    void retiredSizesAreDropped() {
        const auto original = played().toJson();
        auto data = original;
        auto recent = data.value("recent").toArray();
        auto retiredRecord = recent.first().toObject();
        retiredRecord.insert("size", 7);
        recent.append(retiredRecord);
        data.insert("recent", recent);
        auto sizes = data.value("sizes").toObject();
        sizes.insert("8", sizes.value("4"));
        data.insert("sizes", sizes);
        auto current = data.value("current").toObject();
        current.insert("size", 6);
        data.insert("current", current);

        const auto restored = Stats::fromJson(data).toJson();
        QVERIFY(restored.value("sizes").toObject() == original.value("sizes").toObject());
        QVERIFY(restored.value("recent").toArray() == original.value("recent").toArray());
        QCOMPARE(restored.value("current").toObject().value("moves").toInt(), 0);
        QCOMPARE(restored.value("current").toObject().value("size").toInt(), GameRecord{}.size);
        QCOMPARE(restored.value("streak").toInt(), original.value("streak").toInt());
        QCOMPARE(restored.value("bestStreak").toInt(), original.value("bestStreak").toInt());
        QCOMPARE(Stats::fromJson(data).totals(8).games, 0);
    }

    void rejectsMalformedJson_data() {
        QTest::addColumn<int>("mutation");
        const char *names[] = {
            "version", "negative since", "fractional score", "huge count", "bad size", "bad tile",
            "recent overflow", "recent without moves", "missing direction", "string spawn", "streak above best",
            "bad size key", "peaks mismatch", "wins exceed games", "fastest win without win", "current not object",
            "win move after moves", "over not bool",
        };
        for (int index = 0; index < int(std::size(names)); ++index)
            QTest::addRow("%s", names[index]) << index;
    }

    void rejectsMalformedJson() {
        QFETCH(int, mutation);
        auto data = played().toJson();
        const auto edit = [&data](const QString &key, const std::function<void(QJsonObject &)> &change) {
            auto object = data.value(key).toObject();
            change(object);
            data.insert(key, object);
        };
        const auto editSize = [&](const std::function<void(QJsonObject &)> &change) {
            edit("sizes", [&](QJsonObject &sizes) {
                auto totals = sizes.value("4").toObject();
                change(totals);
                sizes.insert("4", totals);
            });
        };
        auto recent = data.value("recent").toArray();
        switch (mutation) {
        case 0: data.insert("version", 2); break;
        case 1: data.insert("since", -1); break;
        case 2: edit("current", [](QJsonObject &record) { record.insert("score", 1.5); }); break;
        case 3: data.insert("directions", QJsonArray{1e300, 0, 0, 0}); break;
        case 4: edit("current", [](QJsonObject &record) { record.insert("size", 2); }); break;
        case 5: edit("current", [](QJsonObject &record) { record.insert("highest", 6); }); break;
        case 6:
            while (recent.size() <= Stats::maximumRecent)
                recent.append(recent.first());
            data.insert("recent", recent);
            break;
        case 7: {
            auto record = recent.first().toObject();
            record.insert("moves", 0);
            record.insert("wonAtMove", 0);
            recent.replace(0, record);
            data.insert("recent", recent);
            break;
        }
        case 8: data.insert("directions", QJsonArray{1, 2, 3}); break;
        case 9: edit("spawns", [](QJsonObject &spawns) { spawns.insert("2", "12"); }); break;
        case 10: data.insert("streak", 1); data.insert("bestStreak", 0); break;
        case 11: edit("sizes", [](QJsonObject &sizes) { sizes.insert("04", sizes.value("4")); }); break;
        case 12: editSize([](QJsonObject &totals) { totals.insert("games", totals.value("games").toInt() + 1); }); break;
        case 13: editSize([](QJsonObject &totals) { totals.insert("wins", totals.value("games").toInt() + 1); }); break;
        case 14: editSize([](QJsonObject &totals) { totals.insert("fastestWin", 3); totals.insert("wins", 0); }); break;
        case 15: data.insert("current", QJsonArray{}); break;
        case 16: edit("current", [](QJsonObject &record) { record.insert("wonAtMove", record.value("moves").toInt() + 2); }); break;
        case 17: edit("current", [](QJsonObject &record) { record.insert("over", 0); }); break;
        }
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Stats::fromJson(data));
    }
};

QTEST_APPLESS_MAIN(StatsTests)
#include "stats_tests.moc"
