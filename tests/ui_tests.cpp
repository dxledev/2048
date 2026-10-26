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
#include "ui/won_overlay.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLocale>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
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

/** @brief Click the center of cell @p index on the window's board. */
void clickCell(const Window &window, int index) {
    QTest::mouseClick(window.board(), Qt::LeftButton, {}, window.board()->cellBounds(index).center().toPoint());
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

    /** @brief Open a window on a saved @p game, so tests can start from any board and charges. */
    std::unique_ptr<Window> windowOn(const Game &game) {
        saveSession(config_ + "/game.json", game, {});
        auto result = window(presetTheme("midnight"), game.size());
        if (!QTest::qWaitForWindowExposed(result.get()))
            qWarning("window was not exposed");
        return result;
    }

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

    void powerBarIsStandardOnly() {
        auto w = window(presetTheme("midnight"));
        QVERIFY(w->powerBar()->isVisible());
        QCOMPARE(w->game().mode(), Mode::PowerUps);
        // The tray sits between the board and the footer, centered.
        QVERIFY(w->powerBar()->geometry().top() >= w->board()->geometry().bottom());
        QVERIFY(qAbs(w->powerBar()->geometry().center().x() - w->board()->geometry().center().x()) <= 1);
        w->startNewGame(4, Mode::Classic);
        QVERIFY(!w->powerBar()->isVisible());
        QCOMPARE(w->game().mode(), Mode::Classic);
        QVERIFY(w->status().contains("Classic"));
        moveOnce(*w);
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(w->status(), QString("Classic mode has no undo"));
        QTest::keyClick(w.get(), Qt::Key_X);
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        w->startNewGame(4);
        QVERIFY(w->powerBar()->isVisible());
    }

    void headerHoldsActionsAboveScores() {
        auto w = window(presetTheme("midnight"));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        auto *stats = buttonNamed(w.get(), "Statistics · T");
        auto *score = w->findChild<QFrame *>("scoreCard");
        QVERIFY(stats && score);
        const auto top = [&](QWidget *widget) { return widget->mapTo(w.get(), QPoint()).y(); };
        QVERIFY(top(stats) + stats->height() <= top(score));
        QVERIFY(top(score) + score->height() <= top(w->board()));
    }

    void wideWindowsSpreadTheHeader() {
        auto w = window(presetTheme("midnight"));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        auto *stats = buttonNamed(w.get(), "Statistics · T");
        auto *score = w->findChild<QFrame *>("scoreCard");
        QVERIFY(stats && score);
        const auto at = [&](QWidget *widget) { return QRect(widget->mapTo(w.get(), QPoint()), widget->size()); };
        w->resize(1600, 900);
        // The score cards join the header row, centered over a centered board, with the tray hanging to its right.
        QTRY_VERIFY(at(score).bottom() > at(stats).top() && at(score).top() < at(stats).bottom());
        QTRY_VERIFY(qAbs(at(w->board()).center().x() - w->width() / 2) <= 2);
        const auto cards = at(score).united(at(w->findChildren<QFrame *>("scoreCard").last()));
        QVERIFY(qAbs(cards.center().x() - w->width() / 2) <= 2);
        QVERIFY(at(w->powerBar()).left() > at(w->board()).right());
        // The header reaches well past the board on both sides.
        auto *brand = w->findChild<QLabel *>("brand");
        QVERIFY(at(brand).left() < at(w->board()).left() - 150);
        QVERIFY(at(buttonNamed(w.get(), "New game · N")).right() > at(w->powerBar()).right() + 50);
        QVERIFY(at(score).bottom() < at(w->board()).top());
        const auto screenshots = qEnvironmentVariable("G2048_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            w->resize(1888, 925);
            QTest::qWait(100);
            QVERIFY(w->grab().save(screenshots + "/widest.png"));
            w->resize(1100, 800);
            QTest::qWait(100);
            QVERIFY(w->grab().save(screenshots + "/between.png"));
        }
        // Back to a narrow window, the cards return to the row under the buttons.
        w->resize(560, 740);
        QTRY_VERIFY(at(score).top() >= at(stats).bottom());
        QTRY_VERIFY(at(score).bottom() <= at(w->board()).top());
    }

    void wideWindowsMoveTheTrayBeside() {
        auto w = window(presetTheme("midnight"));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->resize(1400, 800);
        QTRY_COMPARE(w->powerBar()->orientation(), Qt::Vertical);
        auto *bar = w->powerBar();
        // The layout settles on the next event loop pass.
        QTRY_VERIFY(bar->geometry().left() > w->board()->geometry().right());
        QTest::qWait(50);
        const auto board = w->board()->geometry();
        QVERIFY(bar->geometry().left() > board.right());
        QVERIFY(qAbs(bar->geometry().center().y() - board.center().y()) <= 1);
        QVERIFY(bar->height() > bar->width());
        // The board grows into the height the tray gave up, and the column stays centered.
        const auto *content = w->centralWidget()->findChild<QWidget *>("content");
        const auto margins = content->layout()->contentsMargins();
        QVERIFY(qAbs(margins.left() - margins.right()) <= 1);
        QVERIFY(bar->geometry().right() <= w->width() - margins.right());
        const int wideSide = board.width();
        const auto screenshots = qEnvironmentVariable("G2048_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(w->grab().save(screenshots + "/wide-tray.png"));
            w->showStats();
            QVERIFY(w->grab().save(screenshots + "/stats-wide.png"));
            w->statsModal()->dismiss();
            w->resize(340, 500);
            QTRY_COMPARE(bar->orientation(), Qt::Horizontal);
            QTest::qWait(50);
            QVERIFY(w->grab().save(screenshots + "/narrowest.png"));
            w->showStats();
            QVERIFY(w->grab().save(screenshots + "/stats-narrowest.png"));
            w->statsModal()->dismiss();
        }
        w->resize(560, 800);
        QTRY_COMPARE(bar->orientation(), Qt::Horizontal);
        QTRY_VERIFY(bar->geometry().top() >= w->board()->geometry().bottom());
        QVERIFY(w->board()->width() < wideSide);
        // Classic has no tray, so nothing moves beside the board.
        w->resize(1400, 800);
        w->startNewGame(4, Mode::Classic);
        QVERIFY(!bar->isVisible());
    }

    void undoWorksTwiceThenRefuses() {
        auto w = window(presetTheme("midnight"));
        for (int round = 0; round < 2; ++round) {
            moveOnce(*w);
            QVERIFY(w->powerBar()->button(Power::Undo)->isEnabled());
            QTest::keyClick(w.get(), Qt::Key_U);
            QCOMPARE(w->game().charges(Power::Undo), 1 - round);
        }
        moveOnce(*w);
        QVERIFY(!w->powerBar()->button(Power::Undo)->isEnabled());
        const auto cells = w->game().cells();
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(w->game().cells(), cells);
        QCOMPARE(w->status(), QString("No undos left · make a 128 tile to earn one"));
    }

    void pipsAndTooltipsReflectCharges() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        game.setCharges(Power::Undo, 1);
        game.setCharges(Power::Swap, 0);
        game.setCharges(Power::Remove, 2);
        auto w = windowOn(game);
        auto *bar = w->powerBar();
        QCOMPARE(bar->button(Power::Undo)->charges(), 1);
        QCOMPARE(bar->button(Power::Swap)->charges(), 0);
        QCOMPARE(bar->button(Power::Remove)->charges(), 2);
        QCOMPARE(bar->button(Power::Undo)->toolTip(),
                 QString("Undo · U\nNothing to undo yet. Make a 128 tile to earn +1.\n1 of 2 uses left"));
        QCOMPARE(bar->button(Power::Swap)->toolTip(), QString("Swap two tiles · X\nMake a 256 tile to earn +1.\n0 of 2 uses left"));
        QCOMPARE(bar->button(Power::Remove)->toolTip(),
                 QString("Delete tiles by number · R\nFull. Spend one, then make a 512 tile to earn +1.\n2 of 2 uses left"));
        QVERIFY(!bar->button(Power::Swap)->isEnabled());
        QVERIFY(bar->button(Power::Remove)->isEnabled());
        QCOMPARE(bar->button(Power::Swap)->accessibleName(), QString("Swap two tiles"));
        moveOnce(*w);
        QVERIFY(bar->button(Power::Undo)->isEnabled());
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(bar->button(Power::Undo)->charges(), 0);
        QCOMPARE(bar->button(Power::Undo)->toolTip(), QString("Undo · U\nMake a 128 tile to earn +1.\n0 of 2 uses left"));
    }

    void casualModeHasUnlimitedPowerUps() {
        auto w = window(presetTheme("midnight"));
        w->startNewGame(4, Mode::Casual);
        QCOMPARE(w->game().mode(), Mode::Casual);
        QCOMPARE(w->status(), QString::fromUtf8("New 4×4 game · Casual"));
        auto *bar = w->powerBar();
        QVERIFY(bar->isVisible());
        QVERIFY(bar->button(Power::Undo)->unlimited());
        QVERIFY(bar->button(Power::Swap)->unlimited());
        QVERIFY(bar->button(Power::Remove)->unlimited());
        QCOMPARE(bar->button(Power::Swap)->toolTip(), QString("Swap two tiles · X"));
        // With nothing to undo yet, only undo is disabled.
        QVERIFY(!bar->button(Power::Undo)->isEnabled());
        QVERIFY(bar->button(Power::Swap)->isEnabled());
        QVERIFY(bar->button(Power::Remove)->isEnabled());
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(w->status(), QString("Nothing to undo"));

        // Undo never runs out.
        for (int round = 0; round < 4; ++round) {
            const auto before = w->game().cells();
            moveOnce(*w);
            QVERIFY(bar->button(Power::Undo)->isEnabled());
            QTest::keyClick(w.get(), Qt::Key_U);
            QCOMPARE(w->game().cells(), before);
        }
        QCOMPARE(w->game().charges(Power::Undo), 0);

        // Swap and delete can be armed, and used repeatedly, with no charges.
        Game game(4, Mode::Casual, 1);
        game.setValues({2, 4, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        w.reset();
        w = windowOn(game);
        bar = w->powerBar();
        for (int round = 0; round < 3; ++round) {
            bar->button(Power::Swap)->click();
            QVERIFY(bar->button(Power::Swap)->isChecked());
            QCOMPARE(w->board()->pick(), Board::Pick::Swap);
            clickCell(*w, 0);
            clickCell(*w, 1);
            QCOMPARE(w->board()->pick(), Board::Pick::None);
            QCOMPARE(w->game().at(0).value, round % 2 ? 2 : 4);
        }
        QTest::keyClick(w.get(), Qt::Key_R);
        QCOMPARE(w->board()->pick(), Board::Pick::Remove);
        clickCell(*w, 3);
        QVERIFY(w->game().at(3).value == 0);
        QTest::keyClick(w.get(), Qt::Key_R);
        QCOMPARE(w->board()->pick(), Board::Pick::Remove);
        QTest::keyClick(w.get(), Qt::Key_Escape);
    }

    void casualLossDialogOffersUndo() {
        Game game(4, Mode::Casual, 1);
        game.setValues({2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2});
        auto w = windowOn(game);
        QTRY_VERIFY(w->resultModal()->isVisible());
        auto *secondary = w->resultModal()->findChild<QPushButton *>("secondary");
        QVERIFY(secondary->isVisible());
        QVERIFY(secondary->isEnabled());
        QCOMPARE(secondary->text(), QString("Keep trying"));
        secondary->click();
        QVERIFY(w->status().contains("use a power-up"));
    }

    void earnedChargesAreAnnounced() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        game.setCharges(Power::Undo, 1);
        auto w = windowOn(game);
        QVERIFY(w->move(Direction::Left));
        QCOMPARE(w->game().charges(Power::Undo), 2);
        QCOMPARE(w->status(), QString("+1 Undo · you made a 128"));
        QCOMPARE(w->powerBar()->button(Power::Undo)->charges(), 2);
    }

    void swapTwoTilesByClicking() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        game.setCharges(Power::Swap, 2);
        auto w = windowOn(game);
        auto *swap = w->powerBar()->button(Power::Swap);
        swap->click();
        QCOMPARE(w->board()->pick(), Board::Pick::Swap);
        QVERIFY(swap->isChecked());
        QCOMPARE(w->status(), QString("Pick two tiles to swap · Esc cancels"));
        clickCell(*w, 5); // empty cell: nothing happens
        clickCell(*w, 0);
        clickCell(*w, 1); // equal values are refused and the first pick stays
        QCOMPARE(w->status(), QString("Pick a tile with a different number"));
        QCOMPARE(w->board()->pick(), Board::Pick::Swap);
        QCOMPARE(w->game().charges(Power::Swap), 2);
        clickCell(*w, 2);
        QCOMPARE(w->game().at(0).value, 4);
        QCOMPARE(w->game().at(2).value, 2);
        QCOMPARE(w->game().charges(Power::Swap), 1);
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        QVERIFY(!swap->isChecked());
        QVERIFY(w->board()->animating());
        QCOMPARE(w->powerBar()->button(Power::Swap)->charges(), 1);
        QTRY_VERIFY(QFileInfo::exists(config_ + "/game.json"));
        // Swapping is one undo step.
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(w->game().at(0).value, 2);
    }

    void deleteRemovesEveryTileOfTheClickedValue() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({2, 4, 2, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        game.setCharges(Power::Remove, 2);
        auto w = windowOn(game);
        QTest::keyClick(w.get(), Qt::Key_R);
        QCOMPARE(w->board()->pick(), Board::Pick::Remove);
        QVERIFY(w->powerBar()->button(Power::Remove)->isChecked());
        QCOMPARE(w->status(), QString("Pick a number to delete · Esc cancels"));
        clickCell(*w, 10); // empty
        QCOMPARE(w->game().charges(Power::Remove), 2);
        clickCell(*w, 2);
        for (const auto &tile : w->game().cells())
            QVERIFY(tile.value != 2);
        QCOMPARE(w->game().at(1).value, 4);
        QCOMPARE(w->game().at(4).value, 8);
        QCOMPARE(w->game().charges(Power::Remove), 1);
        QCOMPARE(w->status(), QString("Removed every 2"));
        QCOMPARE(w->board()->pick(), Board::Pick::None);
    }

    void escapeAndMovesCancelPicking() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        game.setCharges(Power::Swap, 2);
        game.setCharges(Power::Remove, 2);
        auto w = windowOn(game);
        QTest::keyClick(w.get(), Qt::Key_X);
        QCOMPARE(w->board()->pick(), Board::Pick::Swap);
        QTest::keyClick(w.get(), Qt::Key_Escape);
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        QVERIFY(!w->powerBar()->button(Power::Swap)->isChecked());
        QCOMPARE(w->game().charges(Power::Swap), 2);

        // The same button disarms; the other button switches; a slide cancels.
        w->powerBar()->button(Power::Swap)->click();
        w->powerBar()->button(Power::Swap)->click();
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        w->armSwap();
        w->armRemove();
        QCOMPARE(w->board()->pick(), Board::Pick::Remove);
        QVERIFY(!w->powerBar()->button(Power::Swap)->isChecked());
        QTest::keyClick(w.get(), Qt::Key_Delete);
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        w->armSwap();
        QTest::keyClick(w.get(), Qt::Key_Right);
        QCOMPARE(w->board()->pick(), Board::Pick::None);
        QCOMPARE(w->game().moves(), 1);

        // Dragging across the board never slides while picking.
        w->armSwap();
        const auto *board = w->board();
        QTest::mousePress(w->board(), Qt::LeftButton, {}, board->cellBounds(0).center().toPoint());
        QTest::mouseRelease(w->board(), Qt::LeftButton, {}, board->cellBounds(3).center().toPoint());
        QCOMPARE(w->game().moves(), 1);
        QCOMPARE(w->board()->pick(), Board::Pick::Swap);
        w->requestNewGame();
        QCOMPARE(w->board()->pick(), Board::Pick::None);
    }

    void newGameModalChoosesModeAndSize() {
        auto w = window(presetTheme("midnight"));
        w->requestNewGame();
        QStringList sizes, modes;
        for (auto *chip : w->newGameModal()->findChildren<QPushButton *>("chip")) {
            if (chip->text().contains(QString::fromUtf8("×")))
                sizes << chip->text();
            else
                modes << chip->text();
        }
        QCOMPARE(sizes, (QStringList{QString::fromUtf8("3×3"), QString::fromUtf8("4×4"), QString::fromUtf8("5×5")}));
        QCOMPARE(modes, (QStringList{"Standard", "Classic", "Casual"}));
        const auto chips = w->newGameModal()->findChildren<QPushButton *>("chip");
        QVERIFY(chips.at(4)->isChecked() || chips.at(3)->isChecked());
        QVERIFY(chips.at(3)->isChecked());
        auto *hint = w->newGameModal()->findChildren<QLabel *>("muted").last();
        QVERIFY(hint->text().startsWith("Undo, swap"));
        chips.at(5)->click();
        QCOMPARE(hint->text(), QString("Unlimited undo, swap, and delete. Just for fun."));
        chips.at(4)->click();
        QCOMPARE(hint->text(), QString("The original game. No power-ups, no undo."));
        chips.at(0)->click();
        QTest::keyClick(w->newGameModal(), Qt::Key_Return);
        QCOMPARE(w->game().mode(), Mode::Classic);
        QCOMPARE(w->game().size(), 3);
        QCOMPARE(w->status(), QString::fromUtf8("New 3×3 game · Classic"));
        QVERIFY(!w->powerBar()->isVisible());
        // The modal preselects the current mode.
        w->requestNewGame();
        QVERIFY(chips.at(4)->isChecked());
        QTest::keyClick(w->newGameModal(), Qt::Key_Escape);
        // "Try again" keeps the mode.
        w->startNewGame(4, Mode::Classic);
        QCOMPARE(w->game().mode(), Mode::Classic);
    }

    void lossDialogOffersPowerUpsInsteadOfUndo() {
        const std::vector<int> stuck{2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};
        Game game(4, Mode::PowerUps, 1);
        game.setValues(stuck);
        auto w = windowOn(game);
        QTRY_VERIFY(w->resultModal()->isVisible());
        auto *secondary = w->resultModal()->findChild<QPushButton *>("secondary");
        QCOMPARE(secondary->text(), QString("Keep trying"));
        QVERIFY(secondary->isEnabled());
        secondary->click();
        QVERIFY(!w->resultModal()->isVisible());
        for (int index = 0; index < 16; ++index)
            QCOMPARE(w->game().at(index).value, stuck[index]);
        QVERIFY(w->status().contains("use a power-up"));
        w.reset();

        game.setCharges(Power::Swap, 0);
        game.setCharges(Power::Undo, 0);
        game.setCharges(Power::Remove, 0);
        w = windowOn(game);
        QTRY_VERIFY(w->resultModal()->isVisible());
        secondary = w->resultModal()->findChild<QPushButton *>("secondary");
        QVERIFY(!secondary->isEnabled());
        QCOMPARE(w->status(), QString("No moves left · start again"));
        w.reset();

        Game classic(4, Mode::Classic, 1);
        classic.setValues(stuck);
        w = windowOn(classic);
        QTRY_VERIFY(w->resultModal()->isVisible());
        QVERIFY(!w->resultModal()->findChild<QPushButton *>("secondary")->isVisible());
        QCOMPARE(w->status(), QString("No moves left · start again"));
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
        Game game(4, Mode::PowerUps, 1);
        game.setValues({1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 100);
        saveSession(config_ + "/game.json", game, {});
        auto w = window(presetTheme("midnight"));
        QVERIFY(w->move(Direction::Left));
        QTRY_VERIFY(w->resultModal()->isVisible());
        QTest::keyClick(w->resultModal(), Qt::Key_Return);
        QVERIFY(w->game().continued());
        w.reset();

        game.setValues({2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2}, 50);
        QVERIFY(game.over());
        saveSession(config_ + "/game.json", game, {BestScores{{4, 900}}, {}});
        w = window(presetTheme("midnight"));
        QTRY_VERIFY(w->resultModal()->isVisible());
        QCOMPARE(w->best(), 900);
    }

    void wonOverlay() {
        Game game(4, Mode::PowerUps, 1);
        game.setValues({1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 100);
        saveSession(config_ + "/game.json", game, {});
        auto w = window(presetTheme("midnight"));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        auto *board = w->board();
        auto *overlay = board->findChild<WonOverlay *>("wonOverlay");
        QVERIFY(overlay);
        QVERIFY(overlay->isHidden());

        // The overlay waits for the merge animation, then fades in.
        QVERIFY(w->move(Direction::Left));
        QVERIFY(w->game().winPending());
        QVERIFY(overlay->isHidden());
        QTRY_VERIFY(overlay->isVisible());
        QCOMPARE(overlay->geometry(), board->boardRect().toRect());
        QCOMPARE(board->accessibleDescription(), QString("You won"));
        // The dialog opens only after the reveal, so its blurred backdrop shows "You won!".
        QTRY_VERIFY(w->resultModal()->isVisible());
        QCOMPARE(overlay->progress(), 1.0);
        const auto screenshots = qEnvironmentVariable("G2048_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QVERIFY(w->grab().save(screenshots + "/won-modal.png"));
        }

        // Esc leaves the win pending; a move re-opens the result instead of sliding.
        QTest::keyClick(w->resultModal(), Qt::Key_Escape);
        QVERIFY(!w->resultModal()->isVisible());
        QVERIFY(w->game().winPending());
        QVERIFY(overlay->isVisible());
        if (!screenshots.isEmpty())
            QVERIFY(w->grab().save(screenshots + "/won-midnight.png"));
        const auto cells = w->game().cells();
        const int moves = w->game().moves();
        QTest::keyClick(w.get(), Qt::Key_Right);
        QCOMPARE(w->game().cells(), cells);
        QCOMPARE(w->game().moves(), moves);
        QVERIFY(w->resultModal()->isVisible());
        QTest::keyClick(w->resultModal(), Qt::Key_Escape);

        // The snapshot follows the theme and the overlay follows the board.
        writeJson(config_ + "/theme.json", presetTheme("paper").toJson());
        QTRY_COMPARE(w->theme().name, QString("paper"));
        QVERIFY(overlay->isVisible());
        QCOMPARE(overlay->progress(), 1.0);
        if (!screenshots.isEmpty())
            QVERIFY(w->grab().save(screenshots + "/won-paper.png"));
        w->resize(w->width() + 120, w->height() + 90);
        QTRY_COMPARE(overlay->geometry(), board->boardRect().toRect());

        // "New game" cancelled keeps the win; "Keep going" ends it.
        QTest::keyClick(w.get(), Qt::Key_Left);
        QVERIFY(w->resultModal()->isVisible());
        auto *secondary = w->resultModal()->findChild<QPushButton *>("secondary");
        QCOMPARE(secondary->text(), QString("New game"));
        secondary->click();
        QVERIFY(w->newGameModal()->isVisible());
        QTest::keyClick(w->newGameModal(), Qt::Key_Escape);
        QVERIFY(w->game().winPending());
        QVERIFY(overlay->isVisible());
        QTest::keyClick(w.get(), Qt::Key_Left);
        QTest::keyClick(w->resultModal(), Qt::Key_Return);
        QVERIFY(!w->resultModal()->isVisible());
        QVERIFY(w->game().continued());
        QVERIFY(overlay->isHidden());
        QCOMPARE(board->accessibleDescription(), QString());
        moveOnce(*w);
        QVERIFY(overlay->isHidden());
        w.reset();

        // A restored pending win shows at launch, and undoing below 2048 clears it.
        saveSession(config_ + "/game.json", game, {});
        w = window(presetTheme("midnight"));
        QVERIFY(w->move(Direction::Left));
        QTRY_VERIFY(w->resultModal()->isVisible());
        QTest::keyClick(w->resultModal(), Qt::Key_Escape);
        w->close();
        w.reset();
        w = window(presetTheme("midnight"));
        QVERIFY(w->game().winPending());
        overlay = w->board()->findChild<WonOverlay *>("wonOverlay");
        QTRY_VERIFY(overlay->isVisible());
        QTRY_VERIFY(w->resultModal()->isVisible());
        QTest::keyClick(w->resultModal(), Qt::Key_Escape);
        QTest::keyClick(w.get(), Qt::Key_U);
        QVERIFY(!w->game().won());
        QVERIFY(overlay->isHidden());
        QVERIFY(w->move(Direction::Left));
        QTRY_VERIFY(overlay->isVisible());
        QVERIFY(overlay->progress() < 1);
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
        QCOMPARE(filters.size(), 4);
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

    void statsAndBestsAreSplitByMode() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        moveOnce(*w);
        const int standardScore = w->game().score();
        w->startNewGame(4, Mode::Classic);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 1);
        QCOMPARE(w->stats(Mode::PowerUps).current().moves, 0);
        QCOMPARE(w->stats(Mode::Classic).totals().games, 0);
        QCOMPARE(w->best(), 0);
        moveOnce(*w);
        QCOMPARE(w->stats(Mode::Classic).totals().games, 1);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 1);
        QTRY_VERIFY(QFileInfo::exists(config_ + "/stats-classic.json"));

        // The panel opens on the live game's mode and the switch shows the other.
        w->showStats();
        auto *panel = w->statsPanel();
        QCOMPARE(panel->mode(), Mode::Classic);
        QCOMPARE(statValue(*w, "Games played"), QString("1"));
        auto *standard = buttonNamed(w->statsModal(), "Standard mode statistics");
        QVERIFY(standard);
        standard->click();
        QCOMPARE(panel->mode(), Mode::PowerUps);
        QCOMPARE(statValue(*w, "Games played"), QString("1"));
        QCOMPARE(statValue(*w, "Best score"), QLocale().toString(standardScore));
        // Resetting Standard leaves the live Classic game and its record alone.
        auto *reset = buttonNamed(w->statsModal(), "Reset statistics");
        reset->click();
        reset->click();
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 0);
        QCOMPARE(w->stats(Mode::Classic).totals().games, 1);
        QCOMPARE(w->stats(Mode::Classic).current().moves, 1);
        w->statsModal()->dismiss();
        w.reset();

        // Both files come back on the next launch.
        w = window(presetTheme("midnight"));
        QCOMPARE(w->game().mode(), Mode::Classic);
        QCOMPARE(w->stats(Mode::Classic).current().moves, 1);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 0);
        w->startNewGame(4, Mode::PowerUps);
        QCOMPARE(w->stats(Mode::Classic).totals().games, 1);
        QCOMPARE(w->stats(Mode::Classic).current().moves, 0);
    }

    void casualStatsAreKeptSeparately() {
        auto w = window(presetTheme("midnight"));
        moveOnce(*w);
        w->startNewGame(4, Mode::Casual);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 1);
        QCOMPARE(w->stats(Mode::Casual).totals().games, 0);
        moveOnce(*w);
        moveOnce(*w);
        QTest::keyClick(w.get(), Qt::Key_U);
        QCOMPARE(w->stats(Mode::Casual).current().moves, 2);
        QCOMPARE(w->stats(Mode::Casual).current().undos, 1);
        QCOMPARE(w->stats(Mode::Classic).totals().games, 0);
        QTRY_VERIFY(QFileInfo::exists(config_ + "/stats-casual.json"));

        // The panel opens on Casual; the other modes are one click away.
        w->showStats();
        auto *panel = w->statsPanel();
        QCOMPARE(panel->mode(), Mode::Casual);
        auto *casual = buttonNamed(w->statsModal(), "Casual mode statistics");
        auto *standard = buttonNamed(w->statsModal(), "Standard mode statistics");
        auto *classic = buttonNamed(w->statsModal(), "Classic mode statistics");
        QVERIFY(casual && standard && classic);
        standard->click();
        QCOMPARE(panel->mode(), Mode::PowerUps);
        QCOMPARE(statValue(*w, "Games played"), QString("1"));
        classic->click();
        QCOMPARE(panel->mode(), Mode::Classic);
        QCOMPARE(statValue(*w, "Games played"), QString("0"));
        casual->click();
        QCOMPARE(panel->mode(), Mode::Casual);
        // Resetting Casual keeps the other modes.
        auto *reset = buttonNamed(w->statsModal(), "Reset statistics");
        reset->click();
        reset->click();
        QCOMPARE(w->stats(Mode::Casual).current().undos, 0);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 1);
        QCOMPARE(w->status(), QString("Casual statistics reset"));
        const int moves = w->stats(Mode::Casual).current().moves;
        w->statsModal()->dismiss();
        w.reset();

        // Casual and its record come back on the next launch.
        w = window(presetTheme("midnight"));
        QCOMPARE(w->game().mode(), Mode::Casual);
        QCOMPARE(w->stats(Mode::Casual).current().moves, moves);
        QCOMPARE(w->stats(Mode::PowerUps).totals().games, 1);
    }

    void statsScrollBarHidesWhenIdle() {
        auto w = window(presetTheme("midnight"));
        w->resize(w->width(), 560);
        w->showStats();
        auto *scroll = w->statsModal()->findChild<QScrollArea *>("statsScroll");
        QVERIFY(scroll);
        auto *bar = scroll->verticalScrollBar();
        auto *content = scroll->widget()->layout();
        QTRY_VERIFY(bar->maximum() > 0);
        // The cards leave the bar's width plus a 5 px gap, whether or not the bar is showing.
        QCOMPARE(content->contentsMargins().right(), bar->sizeHint().width() + 5);
        QVERIFY(!bar->property("idle").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("idle").toBool(), 2000);
        QCOMPARE(content->contentsMargins().right(), bar->sizeHint().width() + 5);
        bar->setValue(bar->maximum());
        QVERIFY(!bar->property("idle").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("idle").toBool(), 2000);
        // Without overflow there is no gutter.
        w->resize(w->width(), 1000);
        QTRY_COMPARE(bar->maximum(), 0);
        QCOMPARE(content->contentsMargins().right(), 0);
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
        QCOMPARE(w->status(), QString("Standard statistics reset"));
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
    }
};

int main(int argc, char **argv) {
    // Modals animate in and out; tests expect them to open and close immediately.
    qputenv("G2048_REDUCED_MOTION", "1");
    QApplication app(argc, argv);
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_tests.moc"
