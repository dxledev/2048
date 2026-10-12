/**
 * @file window.cpp
 * @brief Main game window: layout, input, theming, modals, and persistence.
 *
 * The window owns the Game and a ThemeWatcher. Every theme change, whether it
 * comes from the command line or from a followed palette file, arrives
 * through theme.json and is applied by applyTheme(). The
 * background is painted by a single surface widget so a translucent theme
 * color is composited once rather than once per nested widget.
 */
#include "window.h"
#include "core/color.h"
#include "core/icon.h"
#include "icons.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QPainter>
#include <QShowEvent>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace g2048 {

/** @brief Central widget that paints the theme background exactly once. */
class Surface : public QWidget {
public:
    using QWidget::QWidget;
    QColor color;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), color);
    }
};

namespace {

QLabel *label(const QString &text, const QString &name, QWidget *parent = nullptr) {
    auto *widget = new QLabel(text, parent);
    widget->setObjectName(name);
    return widget;
}

QPushButton *button(const QString &text, const QString &name, const QString &tooltip = {}) {
    auto *widget = new QPushButton(text);
    widget->setObjectName(name);
    widget->setCursor(Qt::PointingHandCursor);
    widget->setFocusPolicy(Qt::NoFocus);
    widget->setToolTip(tooltip);
    widget->setAccessibleName(tooltip.isEmpty() ? text : tooltip);
    return widget;
}

QString number(int value) {
    return QLocale().toString(value);
}

qint64 now() {
    return QDateTime::currentMSecsSinceEpoch();
}

/** @brief Statistics file of @p mode; Standard keeps the name used before modes existed. */
QString statsPath(const QString &directory, Mode mode) {
    return directory + (mode == Mode::Classic ? "/stats-classic.json" : "/stats.json");
}

/** @brief Display name of a power-up. */
QString powerName(Power power) {
    return std::array{"Undo", "Swap two tiles", "Delete tiles by number"}[static_cast<int>(power)];
}

/** @brief Keyboard shortcut of a power-up. */
QString powerKey(Power power) {
    return std::array{"U", "X", "R"}[static_cast<int>(power)];
}

/** @brief Noun for @p power in "No ___ left". */
QString powerNoun(Power power) {
    return std::array{"undos", "swaps", "deletes"}[static_cast<int>(power)];
}

/** @brief Three-line hover text: title and shortcut, how to earn more, and remaining uses. */
QString powerTip(Power power, const Game &game) {
    const int left = game.charges(power), reward = Game::rewardValue(power);
    auto earn = left >= Game::maximumCharges ? QString("Full. Spend one, then make a %1 tile to earn +1.").arg(reward)
                                             : QString("Make a %1 tile to earn +1.").arg(reward);
    if (power == Power::Undo && left > 0 && !game.canUndo())
        earn.prepend("Nothing to undo yet. ");
    return QString("%1 · %2\n%3\n%4 of %5 uses left")
        .arg(powerName(power), powerKey(power), earn)
        .arg(left)
        .arg(Game::maximumCharges);
}

QPixmap glyphPixmap(Glyph glyph, const QColor &color, int size, qreal ratio) {
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintGlyph(painter, glyph, QRectF(0, 0, size, size), color);
    return pixmap;
}

}

Window::Window(QString configDirectory, Theme theme, int size, bool translucent, QWidget *parent)
    : QMainWindow(parent), configDirectory_(std::move(configDirectory)),
      watcher_(new ThemeWatcher(configDirectory_ + "/theme.json", std::move(theme), this)),
      game_(size ? size : 4) {
    setWindowTitle("2048");
    setObjectName("gameWindow");
    setFocusPolicy(Qt::StrongFocus);
    // Wayland surfaces always carry alpha; elsewhere an ARGB window may render black.
    translucent_ = translucent && QGuiApplication::platformName() == "wayland";
    if (translucent_)
        setAttribute(Qt::WA_TranslucentBackground);
    QFont font = this->font();
    font.setFamilies({"Inter", "Adwaita Sans", "Cantarell", "Noto Sans", font.family()});
    setFont(font);
    setMinimumSize(340, 500);
    resize(560, 740);
    buildInterface();
    applyTheme();
    connect(watcher_, &ThemeWatcher::changed, this, [this] {
        applyTheme();
        message("Theme updated · " + watcher_->theme().name);
    });
    connect(watcher_, &ThemeWatcher::rejected, this, [this](const QString &error) {
        message("Theme not loaded: " + error);
    });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, &Window::save);
    gainTimer_.setSingleShot(true);
    connect(&gainTimer_, &QTimer::timeout, gain_, &QLabel::clear);
    statusTimer_.setSingleShot(true);
    connect(&statusTimer_, &QTimer::timeout, this, [this] { status_->clear(); refresh(); });
    resetTimer_.setSingleShot(true);
    resetTimer_.setInterval(3000);
    connect(&resetTimer_, &QTimer::timeout, this, [this] { resetStats_->setText("Reset"); });

    const QString session = configDirectory_ + "/game.json";
    if (QFileInfo::exists(session)) {
        try {
            ModeBests best;
            auto restored = loadSession(session, best);
            best_ = std::move(best);
            if (!size || restored.size() == size) {
                game_ = std::move(restored);
                message(game_.moves() ? "Welcome back. Your board is right where you left it." : "");
            }
        } catch (const std::exception &error) {
            message("Saved game could not be restored: " + QString::fromUtf8(error.what()));
        }
    }
    raiseBest();
    for (const auto mode : {Mode::PowerUps, Mode::Classic}) {
        const QString statistics = statsPath(configDirectory_, mode);
        if (!QFileInfo::exists(statistics))
            continue;
        try {
            stats_[static_cast<int>(mode)] = loadStats(statistics);
        } catch (const std::exception &error) {
            message("Statistics could not be restored: " + QString::fromUtf8(error.what()));
        }
    }
    // Only the live game's mode has a live record; a stray one in the other mode is finished.
    stats_[static_cast<int>(game_.mode() == Mode::Classic ? Mode::PowerUps : Mode::Classic)].finish();
    liveStats().attach(game_, now());
    board_->setGame(&game_);
    board_->animate();
    refresh();
    if (game_.winPending() || game_.over())
        scheduleResult();
}

void Window::buildInterface() {
    surface_ = new Surface;
    surface_->setObjectName("surface");
    setCentralWidget(surface_);
    auto *stack = new QVBoxLayout(surface_);
    stack->setContentsMargins(0, 0, 0, 0);
    content_ = new QWidget;
    content_->setObjectName("content");
    stack->addWidget(content_);
    auto *layout = new QVBoxLayout(content_);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    header_ = buildHeader();
    scoreRow_ = buildScoreRow();
    footer_ = buildFooter();
    board_ = new Board;
    powerBar_ = new PowerBar;
    connect(board_, &Board::swiped, this, &Window::move);
    connect(board_, &Board::picked, this, &Window::pickCell);
    connect(powerBar_, &PowerBar::activated, this, [this](Power power) {
        if (power == Power::Undo)
            undo();
        else
            armPower(power);
    });
    connect(board_, &Board::winRevealed, this, [this] {
        if (resultScheduled_)
            QTimer::singleShot(120, this, &Window::showScheduledResult);
    });
    layout->addWidget(header_);
    layout->addWidget(scoreRow_);
    // Board and tray: a column normally, a row once fitColumn() moves the tray beside the board.
    stage_ = new QWidget;
    middle_ = new QBoxLayout(QBoxLayout::TopToBottom, stage_);
    middle_->setContentsMargins(0, 0, 0, 0);
    middle_->setSpacing(layout->spacing());
    balance_ = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    middle_->addSpacerItem(balance_);
    middle_->addWidget(board_, 1);
    middle_->addWidget(powerBar_, 0, Qt::AlignCenter);
    layout->addWidget(stage_, 1, Qt::AlignHCenter);
    layout->addWidget(footer_);
    buildNewGameModal();
    buildResultModal();
    buildStatsModal();
}

QWidget *Window::buildHeader() {
    auto *header = new QWidget;
    // Equal side columns keep the middle one, which holds the score cards in a wide window, centered.
    headerGrid_ = new QGridLayout(header);
    headerGrid_->setContentsMargins(0, 0, 0, 0);
    headerGrid_->setHorizontalSpacing(12);
    headerGrid_->setColumnStretch(0, 1);
    headerGrid_->setColumnStretch(2, 1);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    brand_ = label({}, "brand");
    brand_->setFixedSize(38, 38);
    row->addWidget(brand_, 0, Qt::AlignVCenter);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(label("2048", "wordmark"));
    titles->addWidget(label("Join the tiles, reach 2048", "muted"));
    row->addLayout(titles);
    headerGrid_->addLayout(row, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    statsButton_ = button("Stats", "tool", "Statistics · T");
    connect(statsButton_, &QPushButton::clicked, this, &Window::showStats);
    newButton_ = button("New game", "primary", "New game · N");
    connect(newButton_, &QPushButton::clicked, this, &Window::requestNewGame);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(statsButton_);
    actions->addWidget(newButton_);
    headerGrid_->addLayout(actions, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    return header;
}

QWidget *Window::buildScoreRow() {
    auto *scores = new QWidget;
    auto *row = new QHBoxLayout(scores);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    status_ = label({}, "status");
    status_->setMinimumWidth(40);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(status_, 1);
    const auto card = [](const QString &title, QLabel *&value, QLabel *extra) {
        auto *frame = new QFrame;
        frame->setObjectName("scoreCard");
        frame->setMinimumWidth(86);
        // The eyebrow row's spacer would otherwise make the card expand with the window.
        frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(14, 7, 14, 8);
        layout->setSpacing(0);
        auto *top = new QHBoxLayout;
        top->setSpacing(6);
        top->addWidget(label(title, "eyebrow"));
        top->addStretch();
        if (extra)
            top->addWidget(extra);
        layout->addLayout(top);
        value = label("0", "scoreValue");
        layout->addWidget(value);
        return frame;
    };
    gain_ = label({}, "gain");
    // The cards sit after the status here, or in the header's middle once fitColumn() moves them.
    scores_ = new QWidget;
    auto *cards = new QHBoxLayout(scores_);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(12);
    cards->addWidget(card("SCORE", score_, gain_));
    cards->addWidget(card("BEST", bestScore_, nullptr));
    row->addWidget(scores_);
    return scores;
}

QWidget *Window::buildFooter() {
    auto *footer = new QWidget;
    auto *row = new QHBoxLayout(footer);
    row->setContentsMargins(2, 0, 2, 0);
    row->setSpacing(12);
    hint_ = label({}, "hint");
    // The hint gives way to the theme name instead of widening the column.
    hint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(hint_, 1);
    themeName_ = label({}, "hint");
    row->addWidget(themeName_);
    return footer;
}

void Window::buildNewGameModal() {
    newGame_ = new Modal(surface_, content_, "New game");
    newGame_->card()->setFixedWidth(420);
    auto *body = newGame_->body();
    body->addWidget(label("New game", "heading"));
    newGameText_ = label({}, "muted");
    newGameText_->setWordWrap(true);
    body->addWidget(newGameText_);
    body->addWidget(label("BOARD SIZE", "eyebrow"));
    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    sizes_ = new QButtonGroup(newGame_);
    for (const int size : {3, 4, 5}) {
        auto *chip = button(QString("%1×%1").arg(size), "chip", QString("%1 by %1 board").arg(size));
        chip->setCheckable(true);
        sizes_->addButton(chip, size);
        chips->addWidget(chip);
    }
    body->addLayout(chips);
    body->addWidget(label("MODE", "eyebrow"));
    auto *modeChips = new QHBoxLayout;
    modeChips->setSpacing(6);
    modes_ = new QButtonGroup(newGame_);
    // Mode::PowerUps is always shown as "Standard".
    for (const auto &[mode, name] : {std::pair{Mode::PowerUps, "Standard"}, std::pair{Mode::Classic, "Classic"}}) {
        auto *chip = button(name, "chip", QString("%1 mode").arg(name));
        chip->setCheckable(true);
        modes_->addButton(chip, static_cast<int>(mode));
        modeChips->addWidget(chip);
    }
    body->addLayout(modeChips);
    modeHint_ = label({}, "muted");
    modeHint_->setWordWrap(true);
    body->addWidget(modeHint_);
    const auto describe = [this] {
        modeHint_->setText(modes_->checkedId() == static_cast<int>(Mode::Classic)
            ? "The original game. No power-ups, no undo."
            : "Undo, swap, and delete power-ups. Big tiles earn more uses.");
    };
    connect(modes_, &QButtonGroup::idClicked, this, describe);
    body->addSpacing(4);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *cancel = button("Cancel", "secondary");
    auto *start = button("Start", "primary");
    connect(cancel, &QPushButton::clicked, newGame_, &Modal::dismiss);
    const auto begin = [this] {
        const int size = sizes_->checkedId() > 0 ? sizes_->checkedId() : game_.size();
        const Mode mode = modes_->checkedId() >= 0 ? static_cast<Mode>(modes_->checkedId()) : game_.mode();
        newGame_->dismiss();
        startNewGame(size, mode);
    };
    connect(start, &QPushButton::clicked, this, begin);
    connect(newGame_, &Modal::accepted, this, begin);
    connect(newGame_, &Modal::dismissed, this, [this] { setFocus(); });
    actions->addWidget(cancel);
    actions->addWidget(start);
    body->addLayout(actions);
}

void Window::buildResultModal() {
    result_ = new Modal(surface_, content_, "Result");
    result_->card()->setFixedWidth(380);
    auto *body = result_->body();
    resultGlyph_ = label({}, "resultGlyph");
    resultGlyph_->setFixedSize(44, 44);
    body->addWidget(resultGlyph_);
    resultTitle_ = label({}, "heading");
    body->addWidget(resultTitle_);
    resultText_ = label({}, "muted");
    resultText_->setWordWrap(true);
    body->addWidget(resultText_);
    body->addSpacing(4);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    resultSecondary_ = button({}, "secondary");
    resultPrimary_ = button({}, "primary");
    actions->addWidget(resultSecondary_);
    actions->addWidget(resultPrimary_);
    body->addLayout(actions);
    // Win: primary keeps playing, secondary starts over. Loss: primary starts over, secondary undoes
    // or, with no undo left, just closes so a swap or delete can be used.
    // Only "Keep going" ends a pending win; any other way out leaves the board marked won.
    connect(resultPrimary_, &QPushButton::clicked, this, [this] {
        if (resultIsWin_) {
            if (game_.winPending()) {
                game_.keepPlaying();
                refresh();
                save();
            }
            result_->dismiss();
        } else {
            result_->dismiss();
            startNewGame(game_.size(), game_.mode());
        }
    });
    connect(resultSecondary_, &QPushButton::clicked, this, [this] {
        const bool win = resultIsWin_;
        result_->dismiss();
        if (win)
            requestNewGame();
        else if (game_.canUndo())
            undo();
    });
    connect(result_, &Modal::accepted, resultPrimary_, &QPushButton::click);
    connect(result_, &Modal::dismissed, this, [this] { setFocus(); });
}

void Window::buildStatsModal() {
    statsModal_ = new Modal(surface_, content_, "Statistics");
    auto *body = statsModal_->body();
    statsPanel_ = new StatsPanel(stats_, game_);
    body->addWidget(statsPanel_);
    auto *actions = new QHBoxLayout;
    resetStats_ = button("Reset", "secondary", "Reset statistics");
    auto *close = button("Close", "primary");
    actions->addWidget(resetStats_);
    actions->addStretch();
    actions->addWidget(close);
    body->addLayout(actions);
    connect(close, &QPushButton::clicked, statsModal_, &Modal::dismiss);
    connect(statsModal_, &Modal::accepted, statsModal_, &Modal::dismiss);
    statsModal_->installEventFilter(statsPanel_);
    // The first click arms the reset for a few seconds; only a second click erases anything.
    connect(resetStats_, &QPushButton::clicked, this, [this] {
        if (!resetTimer_.isActive()) {
            resetStats_->setText("Confirm reset");
            resetTimer_.start();
            return;
        }
        resetTimer_.stop();
        resetStats_->setText("Reset");
        // Only the shown mode is reset; its live record restarts only if the live game is in that mode.
        const Mode shown = statsPanel_->mode();
        auto &stats = stats_[static_cast<int>(shown)];
        if (shown == game_.mode())
            stats.reset(game_, now());
        else
            stats = Stats{};
        save();
        statsPanel_->refresh();
        fitStats();
        message(QString("%1 statistics reset").arg(shown == Mode::Classic ? "Classic" : "Standard"));
    });
    connect(statsModal_, &Modal::dismissed, this, [this] {
        resetTimer_.stop();
        resetStats_->setText("Reset");
        setFocus();
    });
}

void Window::applyTheme() {
    const auto &theme = watcher_->theme();
    QString style = QString::fromUtf8(R"(
        QWidget { background: transparent; color: @text; font-size: 13px; }
        QLabel#wordmark { font-size: 26px; font-weight: 700; }
        QLabel#muted, QLabel#hint, QLabel#tip, QLabel#status { color: @muted; }
        QLabel#hint, QLabel#tip { font-size: 12px; }
        QLabel#heading { font-size: 22px; font-weight: 650; }
        QLabel#eyebrow { color: @muted; font-size: 10px; font-weight: 700; letter-spacing: 1.4px; }
        QLabel#scoreValue { font-size: 21px; font-weight: 700; }
        QLabel#gain { color: @accent; font-size: 11px; font-weight: 700; }
        QFrame#scoreCard { background: @surface; border: 1px solid @border; border-radius: 12px; }
        QFrame#modalCard { background: @surface; border: 1px solid @border; border-radius: 18px; }
        QPushButton { border: 1px solid @border; border-radius: 10px; background: @surface; color: @text;
                      padding: 8px 14px; font-weight: 600; }
        QPushButton:hover { background: @surface_alt; }
        QPushButton:pressed { background: @border; }
        QPushButton:disabled { color: @muted; background: transparent; }
        QPushButton#primary { background: @accent; color: @accent_text; border: 1px solid @accent; }
        QPushButton#primary:hover { background: @accent_hover; border-color: @accent_hover; }
        QPushButton#chip { padding: 9px 4px; }
        QPushButton#chip:checked { background: @accent_soft; border-color: @accent; color: @text; }
        QPushButton#filter { padding: 6px 2px; border-radius: 8px; font-size: 12px; }
        QPushButton#filter:checked { background: @accent_soft; border-color: @accent; color: @text; }
        QFrame#tabBar { background: @surface_alt; border: 1px solid @border; border-radius: 11px; }
        QPushButton#tab { padding: 6px 0; border: 1px solid transparent; border-radius: 8px; font-size: 12px;
                          background: transparent; color: @muted; }
        QPushButton#tab:hover { color: @text; }
        QPushButton#tab:checked { background: @surface; border-color: @border; color: @text; }
        QPushButton#mode { padding: 5px 12px; border: 1px solid transparent; border-radius: 8px; font-size: 12px;
                           background: transparent; color: @muted; }
        QPushButton#mode:hover { color: @text; }
        QPushButton#mode:checked { background: @surface; border-color: @border; color: @text; }
        QFrame#statCard { background: @surface_alt; border: 1px solid @border; border-radius: 12px; }
        QLabel#statValue { font-size: 18px; font-weight: 700; }
        QLabel#factValue { font-size: 15px; font-weight: 700; }
        QLabel#fact { font-weight: 600; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QToolTip { background: @surface_alt; color: @text; border: 1px solid @border; padding: 6px; }
    )");
    const auto surface = theme.color("surface"), accent = theme.color("accent");
    style.replace("@accent_hover", mix(accent, theme.color("text"), .14).name());
    style.replace("@accent_soft", mix(surface, accent, .2).name());
    auto keys = colorKeys();
    // Replace longer keys first so @surface does not clobber @surface_alt.
    std::ranges::sort(keys, [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const auto &key : keys)
        style.replace("@" + key, opaque(theme.color(key)).name());
    setStyleSheet(style);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, opaque(theme.color("background")));
    palette.setColor(QPalette::WindowText, theme.color("text"));
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::Text, theme.color("text"));
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, theme.color("text"));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, theme.color("accent_text"));
    setPalette(palette);
    surface_->color = translucent_ ? theme.color("background") : opaque(theme.color("background"));
    surface_->update();
    const qreal ratio = devicePixelRatioF();
    const auto text = theme.color("text"), muted = theme.color("muted");
    statsButton_->setIcon(glyphIcon(Glyph::Chart, text, muted, ratio));
    newButton_->setIcon(glyphIcon(Glyph::Restart, theme.color("accent_text"), muted, ratio));
    brand_->setPixmap(brandMark(theme, 38, ratio));
    setWindowIcon(applicationIcon(theme));
    try {
        writeIcon(iconPath(configDirectory_), theme);
    } catch (const std::exception &) {
        // A read-only configuration directory only costs the launcher icon its colors.
    }
    themeName_->setText("Theme · " + theme.name);
    board_->setTheme(theme);
    powerBar_->setTheme(theme);
    for (auto *modal : {newGame_, result_, statsModal_})
        modal->setTheme(theme);
    statsPanel_->setTheme(theme);
    if (result_->isVisible())
        resultGlyph_->setPixmap(glyphPixmap(resultIsWin_ ? Glyph::Trophy : Glyph::Grid, accent, 44, ratio));
}

bool Window::modalOpen() const {
    return newGame_->isVisible() || result_->isVisible() || statsModal_->isVisible();
}

QString Window::status() const {
    return status_->text();
}

bool Window::move(Direction direction) {
    if (modalOpen())
        return false;
    cancelPick();
    board_->finish();
    if (game_.winPending()) {
        // A won board stays put until the player chooses to keep going.
        resultScheduled_ = false;
        showResult(true);
        return false;
    }
    if (!game_.move(direction))
        return false;
    liveStats().recordMove(game_, direction, now());
    const int gained = game_.lastTurn().gained;
    raiseBest();
    if (gained) {
        gain_->setText("+" + number(gained));
        gainTimer_.start(650);
    }
    board_->animate();
    refresh();
    QStringList earned, made;
    for (int index = 0; index < Game::powerCount; ++index) {
        const int count = game_.lastTurn().earned[index];
        if (!count)
            continue;
        const auto power = static_cast<Power>(index);
        earned.append(QString("+%1 %2").arg(count).arg(power == Power::Undo ? "Undo" : power == Power::Swap ? "Swap" : "Delete"));
        made.append(QString("a %1").arg(Game::rewardValue(power)));
    }
    if (!earned.isEmpty())
        message(QString("%1 · you made %2").arg(earned.join(", "), made.join(" and ")));
    saveTimer_.start();
    if (game_.winPending() || game_.over())
        scheduleResult();
    return true;
}

void Window::undo() {
    if (modalOpen() && !result_->isVisible())
        return;
    if (game_.mode() == Mode::Classic) {
        message("Classic mode has no undo");
        return;
    }
    cancelPick();
    board_->finish();
    if (!game_.undo()) {
        message(game_.charges(Power::Undo) ? "Nothing to undo"
                                           : QString("No undos left · make a %1 tile to earn one").arg(Game::rewardValue(Power::Undo)));
        return;
    }
    liveStats().recordUndo(game_, now());
    resultScheduled_ = false;
    gain_->clear();
    refresh();
    saveTimer_.start();
}

void Window::armSwap() {
    armPower(Power::Swap);
}

void Window::armRemove() {
    armPower(Power::Remove);
}

void Window::armPower(Power power) {
    if (modalOpen())
        return;
    if (game_.mode() == Mode::Classic) {
        message("Classic mode has no power-ups");
        return;
    }
    const auto pick = power == Power::Swap ? Board::Pick::Swap : Board::Pick::Remove;
    if (board_->pick() == pick) {
        cancelPick();
        return;
    }
    board_->finish();
    if (game_.winPending()) {
        resultScheduled_ = false;
        showResult(true);
        return;
    }
    if (!game_.charges(power)) {
        message(QString("No %1 left · make a %2 tile to earn one").arg(powerNoun(power)).arg(Game::rewardValue(power)));
        return;
    }
    setPick(pick);
}

void Window::cancelPick() {
    if (board_->pick() != Board::Pick::None)
        setPick(Board::Pick::None);
}

void Window::setPick(Board::Pick pick) {
    selected_ = -1;
    board_->setPick(pick);
    powerBar_->setArmed(pick == Board::Pick::None ? std::nullopt
                        : std::optional(pick == Board::Pick::Swap ? Power::Swap : Power::Remove));
    // Drop any transient message so the prompt (or the usual status) shows.
    statusTimer_.stop();
    status_->clear();
    status_->setToolTip({});
    refresh();
}

QString Window::prompt() const {
    switch (board_->pick()) {
    case Board::Pick::Swap:
        return selected_ < 0 ? "Pick two tiles to swap · Esc cancels" : "Pick a second tile · Esc cancels";
    case Board::Pick::Remove: return "Pick a number to delete · Esc cancels";
    case Board::Pick::None: break;
    }
    return {};
}

void Window::pickCell(int index) {
    if (modalOpen() || index < 0 || !game_.at(index).value)
        return;
    if (board_->pick() == Board::Pick::Remove) {
        const int value = game_.at(index).value;
        if (!game_.remove(value))
            return;
        finishPower();
        message(QString("Removed every %1").arg(number(value)));
        return;
    }
    if (selected_ < 0 || index == selected_) {
        selected_ = index == selected_ ? -1 : index;
        board_->setSelected(selected_);
        statusTimer_.stop();
        status_->clear();
        refresh();
        return;
    }
    if (!game_.swap(selected_, index)) {
        message("Pick a tile with a different number");
        return;
    }
    finishPower();
    message("Swapped two tiles");
}

void Window::finishPower() {
    board_->finish();
    setPick(Board::Pick::None);
    board_->animate();
    refresh();
    saveTimer_.start();
    if (game_.winPending() || game_.over())
        scheduleResult();
}

void Window::requestNewGame() {
    if (modalOpen())
        return;
    cancelPick();
    if (auto *chip = sizes_->button(game_.size()))
        chip->setChecked(true);
    if (auto *chip = modes_->button(static_cast<int>(game_.mode())))
        chip->setChecked(true);
    modeHint_->setText(game_.mode() == Mode::Classic ? "The original game. No power-ups, no undo."
                                                     : "Undo, swap, and delete power-ups. Big tiles earn more uses.");
    newGameText_->setText(game_.moves() && !game_.over()
        ? QString("Your current score of %1 will be replaced by a fresh board.").arg(number(game_.score()))
        : QString("Pick a board size and mode. Bigger boards give you more room to build."));
    newGame_->open();
}

void Window::startNewGame(int size, Mode mode) {
    cancelPick();
    board_->finish();
    // The previous game is finished under its own mode, whichever mode comes next.
    if (game_.mode() != mode)
        liveStats().finish();
    game_ = Game(size, mode);
    liveStats().begin(game_, now());
    resultScheduled_ = false;
    raiseBest();
    board_->setGame(&game_);
    board_->animate();
    gain_->clear();
    refresh();
    message(QString("New %1×%1 game").arg(size) + (mode == Mode::Classic ? " · Classic" : ""));
    save();
}

void Window::showStats() {
    if (modalOpen())
        return;
    cancelPick();
    board_->finish();
    statsPanel_->setMode(game_.mode());
    fitStats();
    statsModal_->open();
}

void Window::fitStats() {
    // Modal margins and the card border surround the body.
    const QSize area = surface_->size() - QSize(42, 42);
    const auto margins = statsModal_->body()->contentsMargins();
    const int width = std::min(460, area.width());
    statsModal_->card()->setFixedWidth(width);
    const int footer = resetStats_->sizeHint().height() + statsModal_->body()->spacing();
    statsPanel_->fit(width - margins.left() - margins.right() - 2,
                     area.height() - margins.top() - margins.bottom() - footer);
}

void Window::scheduleResult() {
    if (resultScheduled_)
        return;
    resultScheduled_ = true;
    // A win waits for the board's reveal (Board::winRevealed) so the dialog's backdrop shows it.
    if (game_.winPending() && !board_->showingWin())
        return;
    QTimer::singleShot(game_.winPending() ? 260 : 420, this, &Window::showScheduledResult);
}

void Window::showScheduledResult() {
    if (!resultScheduled_ || modalOpen())
        return;
    resultScheduled_ = false;
    if (game_.winPending())
        showResult(true);
    else if (game_.over())
        showResult(false);
}

void Window::showResult(bool win) {
    cancelPick();
    resultIsWin_ = win;
    const qreal ratio = devicePixelRatioF();
    resultGlyph_->setPixmap(glyphPixmap(win ? Glyph::Trophy : Glyph::Grid, watcher_->theme().color("accent"), 44, ratio));
    if (win) {
        resultTitle_->setText("You made 2048");
        resultText_->setText(QString("Score %1 in %2 %3. Keep going for a bigger tile, or start fresh.")
                                 .arg(number(game_.score()), number(game_.moves()),
                                      game_.moves() == 1 ? "move" : "moves"));
        resultPrimary_->setText("Keep going");
        resultSecondary_->setText("New game");
    } else {
        resultTitle_->setText("No moves left");
        const bool record = game_.score() > 0 && game_.score() >= best();
        resultText_->setText(QString("Final score %1%2. Highest tile %3.")
                                 .arg(number(game_.score()), record ? " · a new best" : QString(" · best ") + number(best()))
                                 .arg(number(game_.highest())));
        resultPrimary_->setText("Try again");
        // Undo if possible; otherwise a spare swap or delete may still free the board.
        const bool spare = game_.charges(Power::Swap) || game_.charges(Power::Remove);
        resultSecondary_->setText(game_.canUndo() || !spare ? "Undo move" : "Keep trying");
        resultSecondary_->setEnabled(game_.canUndo() || spare);
    }
    resultSecondary_->setVisible(win || game_.mode() == Mode::PowerUps);
    if (win)
        resultSecondary_->setEnabled(true);
    result_->open();
}

void Window::refresh() {
    board_->refreshWin();
    score_->setText(number(game_.score()));
    bestScore_->setText(number(best()));
    refreshPowers();
    if (status_->text().isEmpty() || !statusTimer_.isActive()) {
        const bool spare = game_.charges(Power::Undo) || game_.charges(Power::Swap) || game_.charges(Power::Remove);
        if (!prompt().isEmpty())
            status_->setText(prompt());
        else if (game_.over())
            status_->setText(spare ? "No moves left · use a power-up or start again" : "No moves left · start again");
        else if (game_.won())
            status_->setText(QString("Highest tile %1").arg(number(game_.highest())));
        else
            status_->setText(QString("%1×%1%2 · %3 moves").arg(game_.size())
                                 .arg(game_.mode() == Mode::Classic ? " Classic" : "").arg(number(game_.moves())));
    }
}

void Window::refreshPowers() {
    const bool powers = game_.mode() == Mode::PowerUps;
    if (powerBar_->isHidden() == powers) {
        powerBar_->setVisible(powers);
        fitColumn();
    }
    hint_->setText(powers ? "Arrows, WASD, or HJKL to slide · U undo · X swap · R delete"
                          : "Arrows, WASD, or HJKL to slide · swipe works too");
    for (int index = 0; index < Game::powerCount; ++index) {
        const auto power = static_cast<Power>(index);
        auto *item = powerBar_->button(power);
        const auto tip = powerTip(power, game_);
        item->setCharges(game_.charges(power), Game::maximumCharges);
        item->setEnabled(power == Power::Undo ? game_.canUndo() : game_.charges(power) > 0);
        item->setToolTip(tip);
        item->setAccessibleName(powerName(power));
        item->setAccessibleDescription(tip.section('\n', 1).replace('\n', ' '));
    }
}

void Window::message(const QString &text) {
    if (text.isEmpty())
        return;
    status_->setText(text);
    status_->setToolTip(text);
    statusTimer_.start(3200);
}

void Window::raiseBest() {
    auto &best = best_[static_cast<int>(game_.mode())];
    best[game_.size()] = std::max(best.value(game_.size()), game_.score());
}

void Window::save() {
    saveTimer_.stop();
    try {
        saveSession(configDirectory_ + "/game.json", game_, best_);
        for (const auto mode : {Mode::PowerUps, Mode::Classic})
            saveStats(statsPath(configDirectory_, mode), stats_[static_cast<int>(mode)]);
    } catch (const std::exception &error) {
        message("Could not save: " + QString::fromUtf8(error.what()));
    }
}

void Window::keyPressEvent(QKeyEvent *event) {
    if (modalOpen()) {
        event->accept();
        return;
    }
    const bool control = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_Left: case Qt::Key_A: case Qt::Key_H: if (!control) move(Direction::Left); break;
    case Qt::Key_Right: case Qt::Key_D: case Qt::Key_L: if (!control) move(Direction::Right); break;
    case Qt::Key_Up: case Qt::Key_W: case Qt::Key_K: if (!control) move(Direction::Up); break;
    case Qt::Key_Down: case Qt::Key_S: case Qt::Key_J: if (!control) move(Direction::Down); break;
    case Qt::Key_U: case Qt::Key_Z: case Qt::Key_Backspace:
        if (event->key() != Qt::Key_Z || control)
            undo();
        break;
    case Qt::Key_X: if (!control) armSwap(); break;
    case Qt::Key_R: case Qt::Key_Delete: if (!control) armRemove(); break;
    case Qt::Key_Escape:
        if (board_->pick() == Board::Pick::None) {
            QMainWindow::keyPressEvent(event);
            return;
        }
        cancelPick();
        break;
    case Qt::Key_N: requestNewGame(); break;
    case Qt::Key_T: if (!control) showStats(); break;
    default: QMainWindow::keyPressEvent(event); return;
    }
    event->accept();
}

void Window::closeEvent(QCloseEvent *event) {
    liveStats().suspend();
    save();
    QMainWindow::closeEvent(event);
}

void Window::changeEvent(QEvent *event) {
    // Time spent in another window is not play time.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        liveStats().suspend();
    QMainWindow::changeEvent(event);
}

void Window::fitColumn() {
    const int spacing = content_->layout()->spacing();
    const QSize area = surface_->size();
    const bool powers = !powerBar_->isHidden();
    const int titles = headerGrid_->itemAtPosition(0, 0)->sizeHint().height();
    const int actions = headerGrid_->itemAtPosition(0, 2)->sizeHint().height();
    const int cards = scores_->sizeHint().height();
    const int status = std::max(status_->sizeHint().height(), status_->fontMetrics().height());
    struct Plan {
        int chrome, side = 0, extra = 0, below = 0, unit = 0;
        bool sideTray = false;
    };
    // Lay the board out under the chrome height of either header arrangement.
    const auto plan = [&](bool wide) {
        Plan result;
        result.chrome = (wide ? std::max({titles, actions, cards}) + status
                              : std::max(titles, actions) + std::max(status, cards))
            + footer_->sizeHint().height() + spacing * 3;
        const int tall = area.height() - 32 - result.chrome; // Board side when nothing shares its column.
        // Beside the board, the tray needs its own width plus a gap; take that only when the height is the limit anyway.
        const int sideUnit = std::clamp(int(tall * .17), 44, 76), sideGap = 2 * spacing;
        result.sideTray = powers && area.width() - 40 >= tall + sideGap + PowerBar::thicknessFor(sideUnit);
        if (result.sideTray) {
            result.unit = sideUnit;
            result.extra = sideGap + PowerBar::thicknessFor(sideUnit);
            result.side = std::max(board_->minimumSizeHint().width(), tall);
        } else {
            if (powers) {
                // Size the tray from the room the board would have without it, then reserve its height.
                const int rough = std::min(area.width() - 40, tall - PowerBar::thicknessFor(56) - spacing);
                result.unit = std::clamp(int(rough * .17), 44, 76);
                result.below = PowerBar::thicknessFor(result.unit) + spacing;
            }
            result.side = std::max(board_->minimumSizeHint().width(), std::min(area.width() - 40, tall - result.below));
        }
        return result;
    };
    // A wide window spreads the header out and centers the board itself, matching the tray with a spacer.
    auto layout = plan(true);
    const int balanced = layout.side + 2 * layout.extra;
    const bool wide = area.width() - 96 >= std::max(balanced + 240, 760);
    if (!wide)
        layout = plan(false);
    if (wide != wide_) {
        wide_ = wide;
        if (wide) {
            scoreRow_->layout()->removeWidget(scores_);
            headerGrid_->addWidget(scores_, 0, 1, Qt::AlignCenter);
        } else {
            headerGrid_->removeWidget(scores_);
            scoreRow_->layout()->addWidget(scores_);
        }
        status_->setAlignment(wide ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    }
    sideTray_ = layout.sideTray;
    powerBar_->setOrientation(sideTray_ ? Qt::Vertical : Qt::Horizontal);
    if (powers)
        powerBar_->setUnit(layout.unit);
    middle_->setDirection(sideTray_ ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    middle_->setSpacing(sideTray_ ? 2 * spacing : spacing);
    const int balance = wide ? layout.extra : 0;
    balance_->changeSize(balance, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    middle_->invalidate();
    const int stage = layout.side + layout.extra + balance;
    stage_->setFixedWidth(stage);
    // Small windows keep the header and score row readable even when the board is narrower.
    const int column = wide ? std::min(area.width() - 96, std::max(stage + 240, 1200)) : std::max(stage, std::min(area.width() - 24, 360));
    // Collapse header labels to icons when the column is narrow.
    const bool compact = column < 380;
    statsButton_->setText(compact ? QString() : "Stats");
    newButton_->setText(compact ? QString() : "New game");
    const int horizontal = std::max(12, (area.width() - column) / 2);
    const int vertical = std::max(12, (area.height() - layout.side - layout.below - layout.chrome) / 2);
    content_->layout()->setContentsMargins(horizontal, vertical, horizontal, vertical);
    fitStats();
}

void Window::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    fitColumn();
}

void Window::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    fitColumn();
}

}
