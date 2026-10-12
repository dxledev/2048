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
            BestScores best;
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
    best_[game_.size()] = std::max(best_.value(game_.size()), game_.score());
    const QString statistics = configDirectory_ + "/stats.json";
    if (QFileInfo::exists(statistics)) {
        try {
            stats_ = loadStats(statistics);
        } catch (const std::exception &error) {
            message("Statistics could not be restored: " + QString::fromUtf8(error.what()));
        }
    }
    stats_.attach(game_, now());
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
    toolbar_ = buildToolbar();
    footer_ = buildFooter();
    board_ = new Board;
    connect(board_, &Board::swiped, this, &Window::move);
    connect(board_, &Board::winRevealed, this, [this] {
        if (resultScheduled_)
            QTimer::singleShot(120, this, &Window::showScheduledResult);
    });
    layout->addWidget(header_);
    layout->addWidget(toolbar_);
    layout->addWidget(board_, 1);
    layout->addWidget(footer_);
    buildNewGameModal();
    buildResultModal();
    buildStatsModal();
}

QWidget *Window::buildHeader() {
    auto *header = new QWidget;
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    brand_ = label({}, "brand");
    brand_->setFixedSize(38, 38);
    row->addWidget(brand_, 0, Qt::AlignVCenter);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(label("2048", "wordmark"));
    titles->addWidget(label("Join the tiles, reach 2048", "muted"));
    row->addLayout(titles);
    row->addStretch();
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
    row->addWidget(card("SCORE", score_, gain_));
    row->addWidget(card("BEST", bestScore_, nullptr));
    return header;
}

QWidget *Window::buildToolbar() {
    auto *toolbar = new QWidget;
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    status_ = label({}, "status");
    status_->setMinimumWidth(40);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(status_, 1);
    statsButton_ = button("Stats", "tool", "Statistics · T");
    undoButton_ = button("Undo", "tool", "Undo · U");
    connect(statsButton_, &QPushButton::clicked, this, &Window::showStats);
    newButton_ = button("New game", "primary", "New game · N");
    connect(undoButton_, &QPushButton::clicked, this, &Window::undo);
    connect(newButton_, &QPushButton::clicked, this, &Window::requestNewGame);
    row->addWidget(statsButton_);
    row->addWidget(undoButton_);
    row->addWidget(newButton_);
    return toolbar;
}

QWidget *Window::buildFooter() {
    auto *footer = new QWidget;
    auto *row = new QHBoxLayout(footer);
    row->setContentsMargins(2, 0, 2, 0);
    row->addWidget(label("Arrows, WASD, or HJKL to slide · swipe works too", "hint"));
    row->addStretch();
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
    for (const int size : {3, 4, 5, 6, 7, 8}) {
        auto *chip = button(QString("%1×%1").arg(size), "chip", QString("%1 by %1 board").arg(size));
        chip->setCheckable(true);
        sizes_->addButton(chip, size);
        chips->addWidget(chip);
    }
    body->addLayout(chips);
    body->addSpacing(4);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *cancel = button("Cancel", "secondary");
    auto *start = button("Start", "primary");
    connect(cancel, &QPushButton::clicked, newGame_, &Modal::dismiss);
    const auto begin = [this] {
        const int size = sizes_->checkedId() > 0 ? sizes_->checkedId() : game_.size();
        newGame_->dismiss();
        startNewGame(size);
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
    // Win: primary keeps playing, secondary starts over. Loss: primary starts over, secondary undoes.
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
            startNewGame(game_.size());
        }
    });
    connect(resultSecondary_, &QPushButton::clicked, this, [this] {
        const bool win = resultIsWin_;
        result_->dismiss();
        if (win)
            requestNewGame();
        else
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
        stats_.reset(game_, now());
        save();
        statsPanel_->refresh();
        fitStats();
        message("Statistics reset");
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
    undoButton_->setIcon(glyphIcon(Glyph::Undo, text, muted, ratio));
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
    board_->finish();
    if (game_.winPending()) {
        // A won board stays put until the player chooses to keep going.
        resultScheduled_ = false;
        showResult(true);
        return false;
    }
    if (!game_.move(direction))
        return false;
    stats_.recordMove(game_, direction, now());
    const int gained = game_.lastTurn().gained;
    best_[game_.size()] = std::max(best_.value(game_.size()), game_.score());
    if (gained) {
        gain_->setText("+" + number(gained));
        gainTimer_.start(650);
    }
    board_->animate();
    refresh();
    saveTimer_.start();
    if (game_.winPending() || game_.over())
        scheduleResult();
    return true;
}

void Window::undo() {
    if (modalOpen() && !result_->isVisible())
        return;
    board_->finish();
    if (!game_.undo()) {
        message("Nothing to undo");
        return;
    }
    stats_.recordUndo(game_, now());
    resultScheduled_ = false;
    gain_->clear();
    refresh();
    saveTimer_.start();
}

void Window::requestNewGame() {
    if (modalOpen())
        return;
    if (auto *chip = sizes_->button(game_.size()))
        chip->setChecked(true);
    newGameText_->setText(game_.moves() && !game_.over()
        ? QString("Your current score of %1 will be replaced by a fresh board.").arg(number(game_.score()))
        : QString("Pick a board size. Bigger boards give you more room to build."));
    newGame_->open();
}

void Window::startNewGame(int size) {
    board_->finish();
    game_ = Game(size);
    stats_.begin(game_, now());
    resultScheduled_ = false;
    best_[size] = std::max(best_.value(size), 0);
    board_->setGame(&game_);
    board_->animate();
    gain_->clear();
    message(QString("New %1×%1 game").arg(size));
    save();
}

void Window::showStats() {
    if (modalOpen())
        return;
    board_->finish();
    statsPanel_->refresh();
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
        resultSecondary_->setText("Undo move");
        resultSecondary_->setEnabled(game_.canUndo());
    }
    if (win)
        resultSecondary_->setEnabled(true);
    result_->open();
}

void Window::refresh() {
    board_->refreshWin();
    score_->setText(number(game_.score()));
    bestScore_->setText(number(best()));
    undoButton_->setEnabled(game_.canUndo());
    if (status_->text().isEmpty() || !statusTimer_.isActive()) {
        if (game_.over())
            status_->setText("No moves left · undo or start again");
        else if (game_.won())
            status_->setText(QString("Highest tile %1").arg(number(game_.highest())));
        else
            status_->setText(QString("%1×%1 · %2 moves").arg(game_.size()).arg(number(game_.moves())));
    }
}

void Window::message(const QString &text) {
    if (text.isEmpty())
        return;
    status_->setText(text);
    status_->setToolTip(text);
    statusTimer_.start(3200);
}

void Window::save() {
    saveTimer_.stop();
    try {
        saveSession(configDirectory_ + "/game.json", game_, best_);
        saveStats(configDirectory_ + "/stats.json", stats_);
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
    case Qt::Key_N: requestNewGame(); break;
    case Qt::Key_T: if (!control) showStats(); break;
    default: QMainWindow::keyPressEvent(event); return;
    }
    event->accept();
}

void Window::closeEvent(QCloseEvent *event) {
    stats_.suspend();
    save();
    QMainWindow::closeEvent(event);
}

void Window::changeEvent(QEvent *event) {
    // Time spent in another window is not play time.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        stats_.suspend();
    QMainWindow::changeEvent(event);
}

void Window::fitColumn() {
    const int spacing = content_->layout()->spacing();
    const int chrome = header_->sizeHint().height() + toolbar_->sizeHint().height()
        + footer_->sizeHint().height() + spacing * 3;
    const QSize area = surface_->size();
    const int side = std::max(board_->minimumSizeHint().width(), std::min(area.width() - 40, area.height() - 32 - chrome));
    // Collapse toolbar labels to icons when the column is narrow.
    const bool compact = side < 380;
    statsButton_->setText(compact ? QString() : "Stats");
    undoButton_->setText(compact ? QString() : "Undo");
    newButton_->setText(compact ? QString() : "New game");
    const int horizontal = std::max(12, (area.width() - side) / 2);
    const int vertical = std::max(12, (area.height() - side - chrome) / 2);
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
