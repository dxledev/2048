/**
 * @file modal.cpp
 * @brief Blurred, tinted in-window modal layer shared by every dialog.
 *
 * The background is rendered once into a reduced pixmap, padded with repeated
 * edge pixels so the blur has no dark halo, and blurred with a single
 * QGraphicsBlurEffect pass. Only that small image is kept while the modal is
 * open, and it is released as soon as the modal closes. With a translucent
 * theme the blurred image is painted at the background's opacity, so the
 * window keeps the same transparency behind a modal as it has without one.
 */
#include "modal.h"
#include "core/color.h"
#include "modal_motion.h"

#include <QApplication>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <algorithm>

namespace g2048 {
namespace {

QPixmap extendEdges(const QImage &source, int padding) {
    QImage extended(source.size() + QSize(padding * 2, padding * 2), QImage::Format_RGB32);
    for (int y = 0; y < extended.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(std::clamp(y - padding, 0, source.height() - 1)));
        auto *destination = reinterpret_cast<QRgb *>(extended.scanLine(y));
        std::fill_n(destination, padding, row[0]);
        std::copy_n(row, source.width(), destination + padding);
        std::fill_n(destination + padding + source.width(), padding, row[source.width() - 1]);
    }
    return QPixmap::fromImage(extended);
}

}

Modal::Modal(QWidget *parent, QWidget *background, const QString &name)
    : QWidget(parent), background_(background), card_(new QFrame(this)), body_(new QVBoxLayout(card_)) {
    setObjectName(name);
    setAccessibleName(name);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoSystemBackground);
    card_->setObjectName("modalCard");
    body_->setContentsMargins(26, 24, 26, 24);
    body_->setSpacing(14);
    motion_ = new ModalMotion(card_);
    card_->setGraphicsEffect(motion_);
    connect(motion_, &ModalMotion::progressChanged, this, qOverload<>(&QWidget::update));
    connect(motion_, &ModalMotion::finished, this, [this](bool open) {
        if (!open && closing_)
            finishDismiss();
    });
    parent->installEventFilter(this);
    hide();
}

void Modal::setTheme(const Theme &theme) {
    base_ = theme.color("background");
    tint_ = opaque(base_);
    tint_.setAlpha(110);
    if (isVisible())
        capture();
    update();
}

void Modal::open() {
    if (!isVisible()) {
        previousFocus_ = QApplication::focusWidget();
        motion_->reset();
    }
    closing_ = false;
    card_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setGeometry(parentWidget()->rect());
    placeCard();
    capture();
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    motion_->animate(true);
}

void Modal::dismiss() {
    if (!isVisible() || closing_)
        return;
    closing_ = true;
    card_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    motion_->animate(false);
}

void Modal::finishDismiss() {
    closing_ = false;
    hide();
    snapshot_ = QPixmap();
    if (previousFocus_ && previousFocus_->isEnabled() && previousFocus_->isVisible())
        previousFocus_->setFocus();
    previousFocus_.clear();
    emit dismissed();
}

void Modal::capture() {
    if (capturing_ || background_->size().isEmpty())
        return;
    capturing_ = true;
    if (background_->layout())
        background_->layout()->activate();
    const qreal ratio = background_->devicePixelRatioF();
    QImage image(background_->size() * ratio, QImage::Format_RGB32);
    image.setDevicePixelRatio(ratio);
    image.fill(opaque(base_));
    background_->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
    // Blur a reduced copy once; the open modal retains only this small image.
    auto reducedSize = (background_->size() / 3).expandedTo(QSize(1, 1));
    if (reducedSize.width() > 640 || reducedSize.height() > 640)
        reducedSize.scale(640, 640, Qt::KeepAspectRatio);
    auto reduced = image.scaled(reducedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    reduced.setDevicePixelRatio(1);
    constexpr int padding = 32;
    QGraphicsScene scene;
    auto *item = scene.addPixmap(extendEdges(reduced, padding));
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(5);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(blur);
    QPixmap blurred(reduced.size());
    blurred.fill(Qt::transparent);
    QPainter painter(&blurred);
    scene.render(&painter, QRectF(QPointF(), reduced.size()), QRectF(QPointF(padding, padding), reduced.size()));
    painter.end();
    snapshot_ = std::move(blurred);
    capturing_ = false;
    update();
}

void Modal::placeCard() {
    // A layout would ask a wrapping card for its height at the modal's full width,
    // one text line short at the card's own width, and squeeze its buttons.
    if (auto *layout = card_->layout())
        layout->activate();
    const QRect area = rect().marginsRemoved(QMargins(20, 20, 20, 20));
    const int width = std::min(area.width(), std::clamp(card_->sizeHint().width(), card_->minimumWidth(), card_->maximumWidth()));
    const int wanted = card_->hasHeightForWidth() ? card_->heightForWidth(width) : card_->sizeHint().height();
    const int height = std::min(area.height(), std::clamp(wanted, card_->minimumHeight(), card_->maximumHeight()));
    card_->setGeometry(QStyle::alignedRect(layoutDirection(), Qt::AlignCenter, QSize(width, height), area));
}

bool Modal::event(QEvent *event) {
    if (event->type() == QEvent::LayoutRequest)
        placeCard();
    return QWidget::event(event);
}

void Modal::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    placeCard();
}

void Modal::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(base_.alphaF() * motion_->progress());
    painter.drawPixmap(rect(), snapshot_);
    auto tint = tint_;
    tint.setAlphaF(tint.alphaF() * motion_->progress());
    painter.fillRect(rect(), tint);
}

void Modal::mousePressEvent(QMouseEvent *event) {
    event->accept();
    if (!closing_ && dismissable_ && !card_->geometry().contains(event->position().toPoint()))
        dismiss();
}

void Modal::keyPressEvent(QKeyEvent *event) {
    // Consume every key so nothing reaches the board behind the modal.
    event->accept();
    if (closing_)
        return;
    if (event->key() == Qt::Key_Escape && dismissable_)
        dismiss();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        emit accepted();
}

bool Modal::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
        capture();
    }
    if (closing_ && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
                     event->type() == QEvent::Wheel || event->type() == QEvent::ShortcutOverride))
        return true;
    return QWidget::eventFilter(watched, event);
}

}
