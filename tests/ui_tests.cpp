/**
 * @file ui_tests.cpp
 * @brief Window, modal, input, and live theme reload tests.
 *
 * The reload tests model how theme managers switch palettes: a named theme
 * link is atomically retargeted, a selector link flips between a named theme
 * and a generated palette directory, and generated palettes are rewritten
 * either atomically or in place. Every case must recolor an open window and
 * leave the resolved colors saved in theme.json.
 */
#include "core/storage.h"
#include "core/theme_source.h"
#include "ui/window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <memory>

using namespace g2048;

namespace {

void writeQml(const QString &path, const QString &background, const QString &primary) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QString(R"(pragma Singleton
import QtQuick
Singleton {
    readonly property color background: "%1"
    readonly property color foreground: "#f4b999"
    readonly property color primary: "%2"
    readonly property color secondary: "#c85670"
}
)").arg(background, primary).toUtf8());
    QVERIFY(file.commit());
}

/** Replace @p link atomically, the way `ln -s target tmp && mv -T tmp link` does. */
void relink(const QString &link, const QString &target) {
    const QString temporary = link + ".tmp-link";
    QFile::remove(temporary);
    QVERIFY(QFile::link(target, temporary));
    QCOMPARE(std::rename(QFile::encodeName(temporary).constData(), QFile::encodeName(link).constData()), 0);
}

/** @brief Slide in the first direction that changes the board. */
void moveOnce(Window &window) {
    const int before = window.game().moves();
    for (const auto direction : {Direction::Left, Direction::Up, Direction::Right, Direction::Down}) {
        if (window.move(direction))
            break;
    }
    QCOMPARE(window.game().moves(), before + 1);
}

/** @brief Text of the statistics value labeled @p name in the open stats modal. */
QString statValue(const Window &window, const QString &name) {
    for (auto *label : window.statsModal()->findChildren<QLabel *>()) {
        if (label->accessibleName() == name)
            return label->text();
    }
    return {};
}

QPushButton *buttonNamed(const QWidget *parent, const QString &name) {
    for (auto *button : parent->findChildren<QPushButton *>()) {
        if (button->accessibleName() == name)
            return button;
    }
    return nullptr;
}

QString savedColor(const QString &config, const QString &key) {
    return readJson(config + "/theme.json").value("colors").toObject().value(key).toString();
}

}

class UiTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> root_;
    QString themes_, config_;

    std::unique_ptr<Window> window(const Theme &theme, int size = 4) {
        writeJson(config_ + "/theme.json", theme.toJson());
        auto result = std::make_unique<Window>(config_, theme, size, true);
        result->show();
        return result;
    }

private slots:
    void init() {
        root_ = std::make_unique<QTemporaryDir>();
        themes_ = root_->path() + "/themes";
        config_ = root_->path() + "/config";
        qputenv("THEMES_DIR", themes_.toUtf8());
        writeQml(themes_ + "/akane/Colors.qml", "#e00e1e36", "#9279aa");
        writeQml(themes_ + "/nord/Colors.qml", "#2e3440", "#88c0d0");
        writeQml(themes_ + "/.dynamic/generated/Colors.qml", "#0a191c", "#8eb1dd");
        QVERIFY(QFile::link("akane", themes_ + "/current"));
        QVERIFY(QFile::link("current", themes_ + "/.caelestia-use"));
    }

    void cleanup() {
        root_.reset();
    }

    void modalsLiveInsideTheWindow() {
        auto w = window(presetTheme("midnight"));
        for (auto *modal : {w->newGameModal(), w->resultModal()}) {
            QVERIFY(!modal->isWindow());
            QCOMPARE(modal->window(), w.get());
        }
        w->requestNewGame();
        QVERIFY(w->newGameModal()->isVisible());
        QCOMPARE(w->newGameModal()->geometry(), w->centralWidget()->rect());
        QVERIFY(!w->move(Direction::Left) && !w->move(Direction::Right));
        QTest::keyClick(w->newGameModal(), Qt::Key_Escape);
        QVERIFY(!w->newGameModal()->isVisible());
        w->requestNewGame();
        int topLevel = 0;
        for (auto *widget : QApplication::topLevelWidgets())
            topLevel += widget->isVisible();
        QCOMPARE(topLevel, 1);
    }

    void newGameModalChangesBoardSize() {
        auto w = window(presetTheme("midnight"));
        w->requestNewGame();
        auto *chip = w->newGameModal()->findChildren<QPushButton *>("chip").at(2);
        QCOMPARE(chip->text(), QString("5×5"));
        chip->click();
        QTest::keyClick(w->newGameModal(), Qt::Key_Return);
        QCOMPARE(w->game().size(), 5);
        QVERIFY(!w->modalOpen());
        QVERIFY(QFileInfo::exists(config_ + "/game.json"));
    }

    void keyboardMovesAndUndoes() {
        auto w = window(presetTheme("midnight"));
        const auto values = [&] {
            std::vector<int> result;
            for (const auto &tile : w->game().cells())
                result.push_back(tile.value);
            return result;
        };
        const auto before = values();
        for (const auto key : {Qt::Key_Left, Qt::Key_Up, Qt::Key_Right, Qt::Key_Down}) {
            QTest::keyClick(w.get(), key);
            if (w->game().moves())
                break;
        }
        QCOMPARE(w->game().moves(), 1);
        QVERIFY(w->game().canUndo());
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(values(), before);
        QVERIFY(!w->game().canUndo());
    }

    void winAndGameOverOpenResultModal() {
        Game game(4, 1);
        game.setValues({1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 100);
        saveSession(config_ + "/game.json", game, {});
        auto w = window(presetTheme("midnight"));
        QVERIFY(w->move(Direction::Left));
        QTRY_VERIFY(w->resultModal()->isVisible());
        w->resultModal()->dismiss();
        QVERIFY(w->game().continued());
        w.reset();

        game.setValues({2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2}, 50);
        QVERIFY(game.over());
        saveSession(config_ + "/game.json", game, {{4, 900}});
        w = window(presetTheme("midnight"));
        QTRY_VERIFY(w->resultModal()->isVisible());
        QCOMPARE(w->best(), 900);
    }

    void statsModalOpensFromKeyAndButton() {
        auto w = window(presetTheme("midnight"));
        QTest::keyClick(w.get(), Qt::Key_T);
        QVERIFY(w->statsModal()->isVisible());
        QVERIFY(!w->statsModal()->isWindow());
        QCOMPARE(w->statsModal()->geometry(), w->centralWidget()->rect());
        QVERIFY(w->modalOpen());
        QVERIFY(!w->move(Direction::Left) && !w->move(Direction::Right));
        QCOMPARE(w->game().moves(), 0);
        QTest::keyClick(w->statsModal(), Qt::Key_Escape);
        QVERIFY(!w->statsModal()->isVisible());

        auto *button = buttonNamed(w.get(), "Statistics · T");
        QVERIFY(button);
        button->click();
        QVERIFY(w->statsModal()->isVisible());
        QTest::keyClick(w->statsModal(), Qt::Key_Return);
        QVERIFY(!w->modalOpen());
    }

    void statsRecordMovesUndosAndPersist() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        QCOMPARE(w->stats().current().moves, 1);
        long long directions = 0;
        for (const auto count : w->stats().directions())
            directions += count;
        QCOMPARE(directions, 1LL);
        w->undo();
        QCOMPARE(w->stats().current().undos, 1);
        QTRY_VERIFY(QFileInfo::exists(config_ + "/stats.json"));
        QTRY_COMPARE(loadStats(config_ + "/stats.json").current().undos, 1);
        w.reset();

        w = window(presetTheme("midnight"));
        QCOMPARE(w->stats().current().moves, 1);
        QCOMPARE(w->stats().current().undos, 1);
        QCOMPARE(w->stats().totals().games, 1);
    }

    void newGameFinishesTheRecord() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        moveOnce(*w);
        w->startNewGame(4);
        QCOMPARE(w->stats().recent().size(), std::size_t(1));
        QCOMPARE(w->stats().recent().back().moves, 2);
        QCOMPARE(w->stats().current().moves, 0);
        QCOMPARE(w->stats().totals().games, 1);
        // A new game with no moves does not count.
        w->startNewGame(4);
        QCOMPARE(w->stats().totals().games, 1);
    }

    void sizeFilterChangesTotals() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        w->startNewGame(5);
        moveOnce(*w);
        w->showStats();
        QVERIFY(w->statsModal()->isVisible());
        QCOMPARE(statValue(*w, "Games played"), QString("2"));
        const auto filters = w->statsModal()->findChildren<QPushButton *>("filter");
        QCOMPARE(filters.size(), 7);
        QCOMPARE(filters.at(3)->text(), QString("5×5"));
        filters.at(3)->click();
        QCOMPARE(statValue(*w, "Games played"), QString("1"));
        filters.at(1)->click();
        QCOMPARE(statValue(*w, "Games played"), QString("0"));
        QCOMPARE(statValue(*w, "Win rate"), QString::fromUtf8("—"));
        filters.at(0)->click();
        QCOMPARE(statValue(*w, "Games played"), QString("2"));
        QCOMPARE(statValue(*w, "Current game moves"), QString("1"));
    }

    void statsTabsSwitchPages() {
        auto w = window(presetTheme("midnight"));
        w->showStats();
        const auto tabs = w->statsModal()->findChildren<QPushButton *>("tab");
        QCOMPARE(tabs.size(), 4);
        QCOMPARE(tabs.at(0)->text(), QString("Overview"));
        auto *pages = w->statsModal()->findChild<QStackedWidget *>();
        QVERIFY(pages);
        QCOMPARE(pages->currentIndex(), 0);
        tabs.at(2)->click();
        QCOMPARE(pages->currentIndex(), 2);
        QVERIFY(tabs.at(2)->isChecked());
        QTest::keyClick(w->statsModal(), Qt::Key_Tab);
        QCOMPARE(pages->currentIndex(), 3);
        QTest::keyClick(w->statsModal(), Qt::Key_Tab);
        QCOMPARE(pages->currentIndex(), 0);
        QTest::keyClick(w->statsModal(), Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(pages->currentIndex(), 3);
        QTest::keyClick(w->statsModal(), Qt::Key_Left);
        QCOMPARE(pages->currentIndex(), 2);
        QTest::keyClick(w->statsModal(), Qt::Key_2);
        QCOMPARE(pages->currentIndex(), 1);
        QVERIFY(tabs.at(1)->isChecked());
        // Keys stay inside the modal and the board keeps its moves.
        QVERIFY(w->statsModal()->isVisible());
        QCOMPARE(w->game().moves(), 0);
    }

    void statsResetNeedsConfirmation() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        w->startNewGame(4);
        w->showStats();
        auto *reset = buttonNamed(w->statsModal(), "Reset statistics");
        QVERIFY(reset);
        reset->click();
        QCOMPARE(reset->text(), QString("Confirm reset"));
        QCOMPARE(w->stats().totals().games, 1);
        reset->click();
        QCOMPARE(reset->text(), QString("Reset"));
        QCOMPARE(w->stats().totals().games, 0);
        QVERIFY(w->stats().recent().empty());
        QCOMPARE(statValue(*w, "Games played"), QString("0"));
        QCOMPARE(w->status(), QString("Statistics reset"));
        QCOMPARE(loadStats(config_ + "/stats.json").totals().games, 0);
        // Closing the modal disarms a pending reset.
        reset->click();
        w->statsModal()->dismiss();
        QCOMPARE(reset->text(), QString("Reset"));
    }

    void corruptStatsAreReported() {
        QDir().mkpath(config_);
        QFile corrupt(config_ + "/stats.json");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("{ not json");
        corrupt.close();
        auto w = window(presetTheme("midnight"));
        QVERIFY(w->status().startsWith("Statistics could not be restored"));
        QCOMPARE(w->stats().totals().games, 0);
        moveOnce(*w);
        QCOMPARE(w->stats().totals().games, 1);
        w->showStats();
        QVERIFY(w->statsModal()->isVisible());
    }

    void translucentBackgroundKeepsAlpha() {
        auto theme = followTheme("static", "akane");
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        auto w = window(theme);
        QCOMPARE(w->theme().color("background").alpha(), 0xe0);
        QCOMPARE(w->theme().color("background").rgb() & 0xffffff, 0x0e1e36u);
        // Offscreen has no compositor, so the window falls back to the exact RGB channels.
        QCOMPARE(w->translucent(), QGuiApplication::platformName() == "wayland");
    }

    void cliEditsReloadTheOpenWindow() {
        auto w = window(presetTheme("midnight"));
        writeJson(config_ + "/theme.json", setColors(presetTheme("midnight"), {"accent=#ff8800"}).toJson());
        QTRY_COMPARE(w->theme().hex("accent"), QString("#ff8800"));
        QFile corrupt(config_ + "/theme.json");
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("{ not json");
        corrupt.close();
        QSignalSpy rejected(w->themeWatcher(), &ThemeWatcher::rejected);
        QTRY_VERIFY_WITH_TIMEOUT(rejected.count() > 0, 3000);
        QCOMPARE(w->theme().hex("accent"), QString("#ff8800"));
        writeJson(config_ + "/theme.json", presetTheme("paper").toJson());
        QTRY_COMPARE(w->theme().name, QString("paper"));
    }

    void followsEveryKindOfThemeSwitch() {
        auto w = window(followTheme("active"));
        QCOMPARE(w->theme().hex("accent"), QString("#9279aa"));
        QCOMPARE(w->theme().hex("background"), QString("#e00e1e36"));

        // A static theme switch retargets the named-theme link.
        relink(themes_ + "/current", "nord");
        QTRY_COMPARE(w->theme().hex("accent"), QString("#88c0d0"));
        QCOMPARE(savedColor(config_, "accent"), QString("#88c0d0"));

        // Static to generated: the selector flips to the generated palette directory.
        relink(themes_ + "/.caelestia-use", ".dynamic/generated");
        QTRY_COMPARE(w->theme().hex("accent"), QString("#8eb1dd"));
        QCOMPARE(savedColor(config_, "background"), QString("#0a191c"));

        // A new wallpaper regenerates the palette atomically...
        writeQml(themes_ + "/.dynamic/generated/Colors.qml", "#101010", "#e7c092");
        QTRY_COMPARE(w->theme().hex("accent"), QString("#e7c092"));
        // ...or in place.
        QFile file(themes_ + "/.dynamic/generated/Colors.qml");
        QVERIFY(file.open(QIODevice::ReadWrite));
        auto contents = file.readAll().replace("#e7c092", "#a0ccbe");
        file.resize(0);
        file.write(contents);
        file.close();
        QTRY_COMPARE(w->theme().hex("accent"), QString("#a0ccbe"));

        // Back to the static theme, which changed while it was not selected.
        relink(themes_ + "/current", "akane");
        relink(themes_ + "/.caelestia-use", "current");
        QTRY_COMPARE(w->theme().hex("accent"), QString("#9279aa"));
        QCOMPARE(savedColor(config_, "background"), QString("#e00e1e36"));

        // Re-applying the same palette (for example after a shell mode switch) must not repaint.
        QSignalSpy changed(w->themeWatcher(), &ThemeWatcher::changed);
        writeQml(themes_ + "/akane/Colors.qml", "#e00e1e36", "#9279aa");
        relink(themes_ + "/.caelestia-use", "current");
        QTest::qWait(400);
        QCOMPARE(changed.count(), 0);
    }

    void freshConfigFollowsTheActiveTheme() {
        const auto theme = ensureTheme(config_ + "/theme.json");
        QCOMPARE(theme.sourceKind, QString("active"));
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        auto w = window(theme);
        relink(themes_ + "/current", "nord");
        QTRY_COMPARE(w->theme().hex("accent"), QString("#88c0d0"));
        QTRY_VERIFY(QFile(config_ + "/icon.svg").open(QIODevice::ReadOnly));
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
