/**
 * @file stats_view.cpp
 * @brief Statistics modal content: stat cards, painted meters, and the score chart.
 *
 * Charts are painted directly from the palette, like the board, so a theme
 * change only needs a repaint. Bars grow from a shared baseline with rounded
 * data ends; text uses the text and muted colors while marks carry the accent,
 * and won games carry the highlight with a legend so identity never rests on
 * color alone. Standard and Classic games are kept apart and a switch picks
 * which mode is shown. Totals follow the size filter; move directions and
 * spawns are only kept across every size and say so.
 */
#include "stats_view.h"
#include "core/color.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace g2048 {
namespace {

constexpr int rowHeight = 28;
constexpr int barThickness = 8;

QString number(long long value) {
    return QLocale().toString(value);
}

QString percent(double share) {
    const double value = share * 100;
    return QLocale().toString(value, 'f', value > 0 && value < 10 ? 1 : 0) + "%";
}

QString decimal(double value) {
    return QLocale().toString(value, 'f', value < 10 ? 2 : 1);
}

/** @brief Compact play time: "42s", "4m 05s", or "3h 12m". */
QString duration(qint64 milliseconds) {
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    if (seconds < 60)
        return QString("%1s").arg(seconds);
    if (seconds < 3600)
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1h %2m").arg(number(seconds / 3600)).arg(seconds / 60 % 60, 2, 10, QChar('0'));
}

QLabel *label(const QString &text, const QString &name) {
    auto *widget = new QLabel(text);
    widget->setObjectName(name);
    return widget;
}

/** @brief Rounded bar with square corners on its baseline edge at @p baseline. */
QPainterPath roundedBar(const QRectF &bar, double radius, Qt::Edge baseline) {
    QPainterPath path;
    radius = std::min({radius, bar.width() / 2, bar.height() / 2});
    path.addRoundedRect(bar, radius, radius);
    QRectF square = bar;
    if (baseline == Qt::BottomEdge)
        square.setTop(bar.bottom() - radius);
    else
        square.setRight(bar.left() + radius);
    QPainterPath corner;
    corner.addRect(square);
    return path.united(corner);
}

void paintSwatch(QPainter &painter, const Theme &theme, const QRectF &face, int value) {
    const int step = Theme::step(value);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.tiles[step]);
    painter.drawRoundedRect(face, 5, 5);
    QFont font = painter.font();
    font.setPixelSize(value >= 1000 ? 10 : 11);
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(theme.tileText[step]);
    painter.drawText(face, Qt::AlignCenter, QString::number(value));
}

struct Kpi {
    const char *eyebrow;
    const char *name;
    const char *tip;
};

constexpr Kpi kpis[] = {
    {"GAMES", "Games played", "Games with at least one move"},
    {"WINS", "Wins", "Games that reached 2048"},
    {"WIN RATE", "Win rate", "Share of games that reached 2048"},
    {"BEST SCORE", "Best score", "Highest score in a single game"},
    {"AVERAGE", "Average score", "Average final score"},
    {"BEST TILE", "Highest tile", "Largest tile ever made"},
    {"FASTEST WIN", "Fastest win", "Fewest moves needed to reach 2048"},
    {"STREAK", "Win streak", "Current win streak / best win streak, across every size"},
    {"MOVES", "Total moves", "Successful moves, including ones later undone"},
    {"TIME PLAYED", "Time played", "Active play time; idle stretches count at most a minute"},
    {"MERGES", "Merges", "Pairs of tiles merged"},
    {"UNDOS", "Undos used", "Moves taken back"},
};

}

MeterList::MeterList(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MeterList::setRows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    updateGeometry();
    update();
}

void MeterList::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

QSize MeterList::sizeHint() const {
    return {280, int(rows_.size()) * rowHeight};
}

void MeterList::paintEvent(QPaintEvent *) {
    if (rows_.empty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = this->font();
    font.setPixelSize(12);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const bool tiles = std::ranges::any_of(rows_, [](const Row &row) { return row.tile; });
    int lead = tiles ? 38 : 0, trail = 0;
    for (const auto &row : rows_) {
        if (!row.tile)
            lead = std::max(lead, metrics.horizontalAdvance(row.label));
        trail = std::max(trail, metrics.horizontalAdvance(row.value));
    }
    const double barLeft = lead + 10, barRight = width() - trail - 10;
    const auto track = mix(theme_.color("surface"), theme_.color("border"), .7);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const auto &row = rows_[index];
        const QRectF line(0, index * rowHeight, width(), rowHeight);
        if (row.tile) {
            paintSwatch(painter, theme_, QRectF(0, line.center().y() - 11, 38, 22), row.tile);
            painter.setFont(font);
        } else {
            painter.setPen(theme_.color("muted"));
            painter.drawText(QRectF(0, line.top(), lead, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, row.label);
        }
        const QRectF bar(barLeft, line.center().y() - barThickness / 2.0, std::max(0.0, barRight - barLeft), barThickness);
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(bar, barThickness / 2.0, barThickness / 2.0);
        const double fraction = std::clamp(row.fraction, 0.0, 1.0);
        if (fraction > 0) {
            QRectF fill = bar;
            fill.setWidth(std::max(barThickness * 1.0, bar.width() * fraction));
            painter.setBrush(theme_.color("accent"));
            painter.drawPath(roundedBar(fill, barThickness / 2.0, Qt::LeftEdge));
        }
        painter.setPen(theme_.color("text"));
        painter.drawText(QRectF(width() - trail, line.top(), trail, rowHeight), Qt::AlignRight | Qt::AlignVCenter, row.value);
    }
}

ScoreChart::ScoreChart(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ScoreChart::setGames(std::vector<GameRecord> games) {
    games_ = std::move(games);
    hovered_ = -1;
    update();
}

void ScoreChart::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

QRectF ScoreChart::plot() const {
    return QRectF(0, 18, width(), height() - 18 - 26);
}

QRectF ScoreChart::slot(int index) const {
    const auto area = plot();
    const double width = area.width() / std::max<std::size_t>(games_.size(), 12);
    return QRectF(area.left() + index * width, area.top(), width, area.height());
}

void ScoreChart::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = this->font();
    font.setPixelSize(11);
    painter.setFont(font);
    const auto area = plot();
    const auto muted = theme_.color("muted");
    painter.setPen(QPen(theme_.color("border"), 1));
    painter.drawLine(QPointF(area.left(), area.bottom() + .5), QPointF(area.right(), area.bottom() + .5));
    if (games_.empty()) {
        painter.setPen(muted);
        painter.drawText(area, Qt::AlignCenter, "Finished games appear here");
        return;
    }
    const int top = std::max(1, std::ranges::max(games_, {}, &GameRecord::score).score);
    painter.setPen(muted);
    painter.drawText(QRectF(0, 0, width(), 14), Qt::AlignRight | Qt::AlignVCenter, "Best " + number(top));
    painter.drawText(QRectF(0, 0, width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
                     QString("Last %1 %2").arg(games_.size()).arg(games_.size() == 1 ? "game" : "games"));
    for (int index = 0; index < count(); ++index) {
        const auto column = slot(index);
        if (index == hovered_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme_.color("surface_alt"));
            painter.drawRoundedRect(column.adjusted(0, -4, 0, 0), 4, 4);
        }
        const auto &game = games_[index];
        const double height = std::max(2.0, area.height() * game.score / top);
        // A 2px surface gap separates neighboring bars.
        const QRectF bar(column.left() + 1, area.bottom() - height, std::max(1.0, column.width() - 2), height);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.color(game.won() ? "highlight" : "accent"));
        painter.drawPath(roundedBar(bar, 4, Qt::BottomEdge));
    }
    // Two colors need a legend.
    const double y = height() - 12;
    double x = 0;
    for (const auto &[key, text] : {std::pair{"accent", "Final score"}, std::pair{"highlight", "Reached 2048"}}) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.color(key));
        painter.drawRoundedRect(QRectF(x, y - 4, 8, 8), 2, 2);
        painter.setPen(muted);
        const int advance = painter.fontMetrics().horizontalAdvance(text);
        painter.drawText(QRectF(x + 13, y - 8, advance + 2, 16), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += 13 + advance + 16;
    }
}

void ScoreChart::mouseMoveEvent(QMouseEvent *event) {
    int hovered = -1;
    for (int index = 0; index < count(); ++index) {
        if (slot(index).contains(event->position()))
            hovered = index;
    }
    if (hovered != hovered_) {
        hovered_ = hovered;
        update();
    }
    if (hovered < 0) {
        QToolTip::hideText();
        return;
    }
    const auto &game = games_[hovered];
    const QString outcome = game.won() ? QString("Reached 2048 in %1 moves").arg(number(game.wonAtMove))
        : game.over ? QString("No moves left") : QString("Ended early");
    QToolTip::showText(event->globalPosition().toPoint(),
                       QString("Score %1 · %2×%2\nHighest tile %3 · %4 moves\n%5")
                           .arg(number(game.score)).arg(game.size).arg(number(game.highest), number(game.moves), outcome),
                       this, slot(hovered).toRect());
}

void ScoreChart::leaveEvent(QEvent *event) {
    hovered_ = -1;
    update();
    QWidget::leaveEvent(event);
}

StatsPanel::StatsPanel(const std::array<Stats, 2> &stats, const Game &game, QWidget *parent)
    : QWidget(parent), stats_(stats), game_(game) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *titles = titles_ = new QGridLayout;
    titles->setHorizontalSpacing(12);
    titles->setVerticalSpacing(2);
    heading_ = label("Statistics", "heading");
    titles->addWidget(heading_, 0, 0);
    subtitle_ = label({}, "muted");
    titles->addWidget(subtitle_, 1, 0, 1, 2);
    auto *switcher = new QFrame;
    modeSwitch_ = switcher;
    switcher->setObjectName("tabBar");
    auto *modeRow = new QHBoxLayout(switcher);
    modeRow->setContentsMargins(3, 3, 3, 3);
    modeRow->setSpacing(2);
    modes_ = new QButtonGroup(this);
    // Mode::PowerUps is always shown as "Standard".
    for (const auto &[mode, name] : {std::pair{Mode::PowerUps, "Standard"}, std::pair{Mode::Classic, "Classic"}}) {
        auto *button = new QPushButton(name);
        button->setObjectName("mode");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAccessibleName(QString("%1 mode statistics").arg(name));
        modes_->addButton(button, static_cast<int>(mode));
        modeRow->addWidget(button);
    }
    modes_->button(0)->setChecked(true);
    connect(modes_, &QButtonGroup::idClicked, this, &StatsPanel::refresh);
    titles->addWidget(switcher, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
    titles->setColumnStretch(0, 1);
    layout->addLayout(titles);

    auto *bar = new QFrame;
    bar->setObjectName("tabBar");
    auto *tabRow = new QHBoxLayout(bar);
    tabRow->setContentsMargins(3, 3, 3, 3);
    tabRow->setSpacing(2);
    tabs_ = new QButtonGroup(this);
    layout->addWidget(bar);

    auto *chips = new QHBoxLayout;
    chips->setSpacing(4);
    filters_ = new QButtonGroup(this);
    for (const int size : {0, 3, 4, 5}) {
        auto *chip = new QPushButton(size ? QString("%1×%1").arg(size) : QString("All"));
        chip->setObjectName("filter");
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setAccessibleName(size ? QString("%1 by %1 boards").arg(size) : QString("All board sizes"));
        // Chips share the row equally instead of widening the card on narrow windows.
        chip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        filters_->addButton(chip, size);
        chips->addWidget(chip);
    }
    filters_->button(0)->setChecked(true);
    connect(filters_, &QButtonGroup::idClicked, this, &StatsPanel::refresh);
    layout->addLayout(chips);

    scroll_ = new QScrollArea;
    scroll_->setObjectName("statsScroll");
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFocusPolicy(Qt::NoFocus);
    sections_ = new QWidget;
    sections_->setObjectName("statsSections");
    auto *content = new QVBoxLayout(sections_);
    content->setContentsMargins(0, 2, 0, 2);
    pages_ = new QStackedWidget;
    content->addWidget(pages_);
    scroll_->setWidget(sections_);
    layout->addWidget(scroll_);

    const auto tab = [this, tabRow](const QString &title) {
        const int index = pages_->count();
        auto *button = new QPushButton(title);
        button->setObjectName("tab");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(QString("%1 · %2").arg(title).arg(index + 1));
        button->setAccessibleName(title + " tab");
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        tabs_->addButton(button, index);
        tabRow->addWidget(button);
        auto *page = new QWidget;
        auto *column = new QVBoxLayout(page);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(8);
        pages_->addWidget(page);
        return column;
    };
    const auto section = [](QVBoxLayout *page, const QString &title) {
        if (page->count())
            page->addSpacing(8);
        auto *heading = label(title, "eyebrow");
        page->addWidget(heading);
        return heading;
    };

    auto *overview = tab("Overview");
    tiles_ = new QGridLayout;
    tiles_->setSpacing(8);
    for (const auto &kpi : kpis) {
        auto *card = new QFrame;
        card->setObjectName("statCard");
        card->setToolTip(kpi.tip);
        card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto *column = new QVBoxLayout(card);
        column->setContentsMargins(12, 8, 12, 9);
        column->setSpacing(1);
        column->addWidget(label(kpi.eyebrow, "eyebrow"));
        auto *value = label("0", "statValue");
        value->setAccessibleName(kpi.name);
        column->addWidget(value);
        cards_.push_back(card);
        values_.push_back(value);
    }
    overview->addLayout(tiles_);
    overview->addStretch();

    auto *tiles = tab("Tiles");
    section(tiles, "TILES REACHED");
    reached_ = new MeterList;
    reached_->setToolTip("Share of games whose highest tile was at least this tile");
    tiles->addWidget(reached_);
    auto *reachedTip = label("Games whose highest tile was at least each tile.", "tip");
    reachedTip->setWordWrap(true);
    tiles->addWidget(reachedTip);
    tiles->addStretch();

    auto *games = tab("Games");
    currentTitle_ = section(games, {});
    auto *current = currentCard_ = new QFrame;
    current->setObjectName("statCard");
    auto *grid = new QGridLayout(current);
    grid->setContentsMargins(12, 9, 12, 10);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(1);
    int cell = 0;
    for (const char *title : {"SCORE", "MOVES", "BEST TILE", "PLAY TIME", "UNDOS", "MERGES"}) {
        // Rows 0-1 and 3-4 hold eyebrows and values; row 2 separates them.
        const int row = cell / 3 * 3, column = cell % 3;
        grid->addWidget(label(title, "eyebrow"), row, column);
        auto *value = label({}, "factValue");
        value->setAccessibleName(QString("Current game ") + QString(title).toLower());
        grid->addWidget(value, row + 1, column);
        grid->setColumnStretch(column, 1);
        current_.push_back(value);
        ++cell;
    }
    grid->setRowMinimumHeight(2, 8);
    games->addWidget(current);
    section(games, "RECENT GAMES");
    chart_ = new ScoreChart;
    games->addWidget(chart_);
    chartEmpty_ = label({}, "tip");
    chartEmpty_->setWordWrap(true);
    games->addWidget(chartEmpty_);
    games->addStretch();

    auto *style = tab("Style");
    section(style, "MOVES BY DIRECTION · ALL SIZES");
    directions_ = new MeterList;
    directions_->setToolTip("Moves by direction, across every board size");
    style->addWidget(directions_);
    section(style, "PACE");
    auto *facts = new QGridLayout;
    facts->setHorizontalSpacing(12);
    facts->setVerticalSpacing(6);
    int line = 0;
    for (const char *title : {"Spawned 4s", "Moves per game", "Merges per move", "Time per game"}) {
        facts->addWidget(label(title, "muted"), line, 0);
        auto *value = label({}, "fact");
        value->setAccessibleName(title);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        facts->addWidget(value, line++, 1);
        facts_.push_back(value);
    }
    facts->setColumnStretch(0, 1);
    style->addLayout(facts);
    style->addStretch();

    connect(tabs_, &QButtonGroup::idClicked, this, &StatsPanel::setPage);
    setPage(0);
}

void StatsPanel::setPage(int index) {
    index = (index % pageCount() + pageCount()) % pageCount();
    tabs_->button(index)->setChecked(true);
    pages_->setCurrentIndex(index);
    scroll_->verticalScrollBar()->setValue(0);
}

bool StatsPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    // Seen before the modal, so Tab never moves focus out to the board.
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Tab || key == Qt::Key_Right)
        setPage(page() + 1);
    else if (key == Qt::Key_Backtab || key == Qt::Key_Left)
        setPage(page() - 1);
    else if (key >= Qt::Key_1 && key < Qt::Key_1 + pageCount())
        setPage(key - Qt::Key_1);
    else
        return QWidget::eventFilter(watched, event);
    return true;
}

int StatsPanel::filter() const {
    return std::max(0, filters_->checkedId());
}

Mode StatsPanel::mode() const {
    return static_cast<Mode>(std::max(0, modes_->checkedId()));
}

void StatsPanel::setMode(Mode mode) {
    modes_->button(static_cast<int>(mode))->setChecked(true);
    refresh();
}

void StatsPanel::refresh() {
    const int size = filter();
    const auto &stats = stats_[static_cast<int>(mode())];
    // The live game only shows under its own mode.
    const bool live = game_.mode() == mode();
    const auto totals = stats.totals(size);
    const bool any = totals.games > 0;
    const QString board = size ? QString("%1×%1 ").arg(size) : QString();
    if (any && stats.since())
        subtitle_->setText(QString("Since %1 · %2 %3")
                               .arg(QLocale().toString(QDateTime::fromMSecsSinceEpoch(stats.since()).date(), "d MMM yyyy"),
                                    number(totals.games), board + (totals.games == 1 ? "game" : "games")));
    else
        subtitle_->setText(QString("No %1games yet").arg(board));

    const QString none = QString::fromUtf8("—");
    const QStringList values{
        number(totals.games),
        number(totals.wins),
        any ? percent(totals.winRate()) : none,
        number(totals.bestScore),
        any ? number(std::llround(totals.averageScore())) : none,
        totals.bestTile ? number(totals.bestTile) : none,
        totals.fastestWin ? number(totals.fastestWin) : none,
        QString("%1 / %2").arg(number(stats.currentStreak()), number(stats.bestStreak())),
        number(totals.moves),
        duration(totals.played),
        number(totals.merges),
        number(totals.undos),
    };
    for (int index = 0; index < values.size(); ++index)
        values_[index]->setText(values[index]);

    std::vector<MeterList::Row> reached;
    const int last = std::max(Game::winningValue, totals.bestTile * 2);
    for (int tile = 128; tile > 0 && tile <= last && tile <= (1 << 30); tile *= 2) {
        const int count = totals.reached(tile);
        reached.push_back({{}, tile, any ? double(count) / totals.games : 0,
                           any ? QString("%1 · %2").arg(number(count), percent(double(count) / totals.games)) : QString("0")});
    }
    reached_->setRows(std::move(reached));

    std::vector<GameRecord> recent;
    for (const auto &record : stats.recent()) {
        if (!size || record.size == size)
            recent.push_back(record);
    }
    chart_->setGames(std::move(recent));
    chartEmpty_->setVisible(!chart_->count() && (!live || game_.moves()));
    chartEmpty_->setText(live ? QString("The current game joins the chart once you start a new one.")
                              : QString("No finished %1 games yet.").arg(mode() == Mode::Classic ? "Classic" : "Standard"));

    const auto &record = stats.current();
    currentTitle_->setVisible(live);
    currentCard_->setVisible(live);
    currentTitle_->setText(QString("CURRENT GAME · %1×%1%2").arg(game_.size())
                               .arg(game_.over() ? " · OVER" : game_.won() ? " · WON" : ""));
    const QStringList current{
        number(game_.score()),
        number(game_.moves()),
        number(std::max(record.highest, game_.highest())),
        duration(record.played),
        number(record.undos),
        number(record.merges),
    };
    for (int index = 0; index < current.size(); ++index)
        current_[index]->setText(current[index]);

    const auto &directions = stats.directions();
    long long moves = 0;
    for (const auto count : directions)
        moves += count;
    std::vector<MeterList::Row> rows;
    const char *names[] = {"← Left", "→ Right", "↑ Up", "↓ Down"};
    for (int index = 0; index < 4; ++index) {
        const double share = moves ? double(directions[index]) / moves : 0;
        rows.push_back({QString::fromUtf8(names[index]), 0, share,
                        moves ? QString("%1 · %2").arg(number(directions[index]), percent(share)) : QString("0")});
    }
    directions_->setRows(std::move(rows));
    const long long spawned = stats.spawnedTwos() + stats.spawnedFours();
    const QStringList facts{
        spawned ? QString("%1 of %2").arg(percent(double(stats.spawnedFours()) / spawned), number(spawned)) : none,
        any ? number(std::llround(double(totals.moves) / totals.games)) : none,
        totals.moves ? decimal(double(totals.merges) / totals.moves) : none,
        any ? duration(totals.played / totals.games) : none,
    };
    for (int index = 0; index < facts.size(); ++index)
        facts_[index]->setText(facts[index]);
    sections_->adjustSize();
}

void StatsPanel::setTheme(const Theme &theme) {
    reached_->setTheme(theme);
    directions_->setTheme(theme);
    chart_->setTheme(theme);
}

void StatsPanel::fit(int width, int height) {
    setFixedWidth(width);
    // Beside the heading the switch needs its natural width; narrow cards give it a row of its own.
    const bool stacked = width < heading_->sizeHint().width() + modeSwitch_->sizeHint().width() + 12;
    if (stacked != stacked_) {
        stacked_ = stacked;
        titles_->removeWidget(modeSwitch_);
        if (stacked)
            titles_->addWidget(modeSwitch_, 3, 0, 1, 2);
        else
            titles_->addWidget(modeSwitch_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        titles_->setRowMinimumHeight(2, stacked ? 6 : 0);
    }
    const int columns = width >= 360 ? 3 : 2;
    if (columns != columns_) {
        columns_ = columns;
        for (auto *card : cards_)
            tiles_->removeWidget(card);
        for (int index = 0; index < int(cards_.size()); ++index)
            tiles_->addWidget(cards_[index], index / columns, index % columns);
        for (int column = 0; column < 3; ++column)
            tiles_->setColumnStretch(column, column < columns);
    }
    auto *layout = this->layout();
    int fixed = 0;
    for (int index = 0; index < layout->count(); ++index) {
        auto *item = layout->itemAt(index);
        if (item->widget() != scroll_)
            fixed += item->sizeHint().height() + layout->spacing();
    }
    sections_->layout()->activate();
    const int content = sections_->layout()->sizeHint().height();
    const int available = std::max(140, height - fixed);
    const bool scrolling = content > available;
    // Keep cards clear of the scroll bar when one is shown.
    sections_->layout()->setContentsMargins(0, 2, scrolling ? scroll_->verticalScrollBar()->sizeHint().width() + 6 : 0, 2);
    scroll_->setFixedHeight(std::min(content, available));
}

}
