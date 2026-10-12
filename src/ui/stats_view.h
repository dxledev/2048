#pragma once

#include "core/stats.h"
#include "core/theme.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWidget>
#include <array>
#include <vector>

namespace g2048 {

/** @brief Painted list of horizontal bars, each led by a label or a tile swatch. */
class MeterList : public QWidget {
public:
    struct Row {
        QString label; ///< Leading text, unused when @c tile is set.
        int tile = 0; ///< Face value drawn as a tile swatch instead of the label.
        double fraction = 0; ///< Filled share of the bar, from 0 to 1.
        QString value; ///< Trailing value text.
    };
    explicit MeterList(QWidget *parent = nullptr);
    void setRows(std::vector<Row> rows);
    void setTheme(const Theme &theme);
    const std::vector<Row> &rows() const { return rows_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {160, sizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::vector<Row> rows_;
    Theme theme_;
};

/** @brief Painted bar chart of recent final scores with a per-bar hover tooltip. */
class ScoreChart : public QWidget {
public:
    explicit ScoreChart(QWidget *parent = nullptr);
    /** @brief Show @p games oldest first; the chart keeps its own copy. */
    void setGames(std::vector<GameRecord> games);
    void setTheme(const Theme &theme);
    int count() const { return int(games_.size()); }
    QSize sizeHint() const override { return {320, 150}; }
    QSize minimumSizeHint() const override { return {160, 150}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    std::vector<GameRecord> games_;
    Theme theme_;
    int hovered_ = -1;
    QRectF plot() const;
    /** @brief Full-height column of game @p index, the bar's hit target. */
    QRectF slot(int index) const;
};

/**
 * @brief Content of the statistics modal: heading, mode switch, tabs, size filter, and the tab pages.
 *
 * The panel reads the Stats of each mode and the Game owned by the window and rebuilds its text
 * from them on refresh(). Only the page below the filter scrolls; every page
 * takes the height of the tallest so the card keeps its size between tabs.
 */
class StatsPanel : public QWidget {
    Q_OBJECT
public:
    /** @param stats Statistics of each mode, indexed by Mode. */
    explicit StatsPanel(const std::array<Stats, 2> &stats, const Game &game, QWidget *parent = nullptr);
    /** @brief Board size the totals are filtered to, or 0 for every size. */
    int filter() const;
    /** @brief Mode whose statistics are shown. */
    Mode mode() const;
    /** @brief Show the statistics of @p mode. */
    void setMode(Mode mode);
    /** @brief Index of the shown tab. */
    int page() const { return pages_->currentIndex(); }
    int pageCount() const { return pages_->count(); }
    /** @brief Show tab @p index, wrapping around at either end. */
    void setPage(int index);
    /** @brief Re-read the statistics and the live game. */
    void refresh();
    void setTheme(const Theme &theme);
    /** @brief Lay out for @p width pixels and at most @p height pixels in total. */
    void fit(int width, int height);

protected:
    /** @brief Install on the hosting modal: Tab and the arrows cycle tabs, digits pick one. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const std::array<Stats, 2> &stats_;
    const Game &game_;
    QLabel *heading_, *subtitle_;
    QGridLayout *titles_;
    QWidget *modeSwitch_;
    QButtonGroup *modes_, *tabs_, *filters_;
    QScrollArea *scroll_;
    QWidget *sections_;
    QStackedWidget *pages_;
    QGridLayout *tiles_;
    std::vector<QWidget *> cards_;
    std::vector<QLabel *> values_;
    MeterList *reached_, *directions_;
    ScoreChart *chart_;
    QWidget *currentCard_;
    QLabel *chartEmpty_, *currentTitle_;
    std::vector<QLabel *> current_, facts_;
    int columns_ = 0;
    bool stacked_ = false; ///< The mode switch has its own row under the heading.
};

}
