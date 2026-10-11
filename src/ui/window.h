#pragma once

#include "board.h"
#include "core/storage.h"
#include "modal.h"
#include "stats_view.h"
#include "theme_watcher.h"
#include <QButtonGroup>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>

namespace g2048 {

class Surface;

/** @brief Main window: board, score header, toolbar, and in-window modals. */
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
    const Theme &theme() const { return watcher_->theme(); }
    ThemeWatcher *themeWatcher() const { return watcher_; }
    /** @brief Whether the window background is drawn with the theme's alpha. */
    bool translucent() const { return translucent_; }
    int best() const { return best_.value(game_.size()); }
    Modal *newGameModal() const { return newGame_; }
    Modal *resultModal() const { return result_; }
    Modal *statsModal() const { return statsModal_; }
    const Stats &stats() const { return stats_; }
    /** @brief Return whether any modal currently covers the board. */
    bool modalOpen() const;
    /** @brief Return the transient status line text. */
    QString status() const;

public slots:
    /** @brief Slide the tiles; returns whether the board changed. */
    bool move(Direction direction);
    /** @brief Revert the last move. */
    void undo();
    /** @brief Open the new-game modal. */
    void requestNewGame();
    /** @brief Replace the current game with a fresh board of @p size. */
    void startNewGame(int size);
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
    BestScores best_;
    Stats stats_;
    bool translucent_ = false;
    Surface *surface_;
    QWidget *content_, *header_, *toolbar_, *footer_;
    Board *board_;
    QLabel *brand_, *score_, *bestScore_, *gain_, *status_, *themeName_;
    QPushButton *statsButton_, *undoButton_, *newButton_;
    Modal *newGame_, *result_, *statsModal_;
    StatsPanel *statsPanel_;
    QPushButton *resetStats_;
    QButtonGroup *sizes_;
    QLabel *newGameText_, *resultGlyph_, *resultTitle_, *resultText_;
    QPushButton *resultPrimary_, *resultSecondary_;
    QTimer saveTimer_, gainTimer_, statusTimer_, resetTimer_;
    bool resultIsWin_ = false;
    bool resultScheduled_ = false;

    void buildInterface();
    QWidget *buildHeader();
    QWidget *buildToolbar();
    QWidget *buildFooter();
    void buildNewGameModal();
    void buildResultModal();
    void buildStatsModal();
    /** @brief Size the statistics card to the window. */
    void fitStats();
    void applyTheme();
    /** @brief Center a column as wide as the largest board that fits the window. */
    void fitColumn();
    /** @brief Refresh score labels and undo availability from game state. */
    void refresh();
    /** @brief Show the win or game-over modal once the move animation settles. */
    void scheduleResult();
    void showResult(bool win);
    void message(const QString &text);
    void save();
};

}
