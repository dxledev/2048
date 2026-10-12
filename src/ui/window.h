#pragma once

#include "board.h"
#include "core/storage.h"
#include "modal.h"
#include "power_bar.h"
#include "stats_view.h"
#include "theme_watcher.h"
#include <QBoxLayout>
#include <QButtonGroup>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>

namespace g2048 {

class Surface;

/** @brief Main window: header, score row, board, power-up tray, and in-window modals. */
class Window : public QMainWindow {
    Q_OBJECT
public:
    /**
     * @brief Build the window and restore the saved game or start a new one.
     * @param configDirectory Directory holding theme.json and game.json.
     * @param theme Initial validated theme.
     * @param size Board size for a fresh game, or zero to resume the saved game.
     * @param translucent Let a translucent theme background reach the compositor when supported.
     */
    Window(QString configDirectory, Theme theme, int size = 0, bool translucent = true, QWidget *parent = nullptr);
    const Game &game() const { return game_; }
    Board *board() const { return board_; }
    PowerBar *powerBar() const { return powerBar_; }
    const Theme &theme() const { return watcher_->theme(); }
    ThemeWatcher *themeWatcher() const { return watcher_; }
    /** @brief Whether the window background is drawn with the theme's alpha. */
    bool translucent() const { return translucent_; }
    /** @brief Best score for the current game's size and mode. */
    int best() const { return best_[static_cast<int>(game_.mode())].value(game_.size()); }
    Modal *newGameModal() const { return newGame_; }
    Modal *resultModal() const { return result_; }
    Modal *statsModal() const { return statsModal_; }
    /** @brief Statistics of the current game's mode. */
    const Stats &stats() const { return stats(game_.mode()); }
    const Stats &stats(Mode mode) const { return stats_[static_cast<int>(mode)]; }
    StatsPanel *statsPanel() const { return statsPanel_; }
    /** @brief Return whether any modal currently covers the board. */
    bool modalOpen() const;
    /** @brief Return the transient status line text. */
    QString status() const;

public slots:
    /** @brief Slide the tiles; returns whether the board changed. */
    bool move(Direction direction);
    /** @brief Revert the last move, spending an Undo charge. */
    void undo();
    /** @brief Arm swapping two tiles (or disarm it when armed); the next two board clicks pick the tiles. */
    void armSwap();
    /** @brief Arm deleting every tile of one value (or disarm it when armed); the next board click picks the value. */
    void armRemove();
    /** @brief Leave pick mode without spending anything. */
    void cancelPick();
    /** @brief Open the new-game modal. */
    void requestNewGame();
    /** @brief Replace the current game with a fresh board of @p size in @p mode. */
    void startNewGame(int size, Mode mode = Mode::PowerUps);
    /** @brief Open the statistics modal. */
    void showStats();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QString configDirectory_;
    ThemeWatcher *watcher_;
    Game game_;
    ModeBests best_;
    std::array<Stats, 2> stats_; ///< Indexed by Mode.
    bool sideTray_ = false; ///< The power-up tray stands beside the board rather than under it.
    bool wide_ = false; ///< The header spans the window with the score cards in its middle.
    bool translucent_ = false;
    Surface *surface_;
    QWidget *content_, *header_, *scoreRow_, *scores_, *stage_, *footer_;
    QGridLayout *headerGrid_;
    QBoxLayout *middle_;
    QSpacerItem *balance_; ///< Matches the side tray on the board's left so the board stays centered.
    Board *board_;
    QLabel *brand_, *score_, *bestScore_, *gain_, *status_, *themeName_, *hint_;
    QPushButton *statsButton_, *newButton_;
    PowerBar *powerBar_;
    Modal *newGame_, *result_, *statsModal_;
    StatsPanel *statsPanel_;
    QPushButton *resetStats_;
    QButtonGroup *sizes_, *modes_;
    QLabel *newGameText_, *modeHint_, *resultGlyph_, *resultTitle_, *resultText_;
    QPushButton *resultPrimary_, *resultSecondary_;
    QTimer saveTimer_, gainTimer_, statusTimer_, resetTimer_;
    bool resultIsWin_ = false;
    bool resultScheduled_ = false;
    int selected_ = -1; ///< First tile of a swap in progress, or -1.

    void buildInterface();
    QWidget *buildHeader();
    QWidget *buildScoreRow();
    QWidget *buildFooter();
    void buildNewGameModal();
    void buildResultModal();
    void buildStatsModal();
    /** @brief Size the statistics card to the window. */
    void fitStats();
    void applyTheme();
    /** @brief Arm @p power, or disarm it when already armed. */
    void armPower(Power power);
    /** @brief Enter pick mode (or leave it for Pick::None) and sync the board, tray, and prompt. */
    void setPick(Board::Pick pick);
    /** @brief Handle a board click in pick mode. */
    void pickCell(int index);
    /** @brief Animate, refresh, save, and queue a result after a swap or delete. */
    void finishPower();
    /** @brief Status line for pick mode, or empty when not picking. */
    QString prompt() const;
    /** @brief Update pips, enabled state, and tooltips of the power-up tray. */
    void refreshPowers();
    /**
     * @brief Center a column as wide as the largest board that fits the window.
     *
     * When the window is wide enough to spare the room, the power-up tray moves
     * beside the board, which lets the board grow into the height it gave up.
     * With room to spare beyond that, the header spans most of the window and
     * the score cards move up into its middle.
     */
    void fitColumn();
    /** @brief Refresh score labels, power-up tray, and status from game state. */
    void refresh();
    /** @brief Show the win or game-over modal once the move animation settles. */
    void scheduleResult();
    void showScheduledResult();
    void showResult(bool win);
    void message(const QString &text);
    Stats &liveStats() { return stats_[static_cast<int>(game_.mode())]; }
    void raiseBest();
    void save();
};

}
