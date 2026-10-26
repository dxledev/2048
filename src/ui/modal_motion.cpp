#include "modal_motion.h"
#include <QPainter>
#include <QScreen>
#include <QWidget>
#include <cmath>
namespace g2048 {
ModalMotion::ModalMotion(QObject *parent) : QGraphicsEffect(parent) {
  setObjectName("modalMotion");
  timeline_.setParent(this);
  timeline_.setObjectName("modalTimeline");
  timeline_.setDuration(duration);
  timeline_.setEasingCurve(QEasingCurve::OutCubic);
  connect(&timeline_, &QTimeLine::valueChanged, this, [this](qreal value) {
    setProgress(startProgress_ + ((targetOpen_ ? 1.0 : 0.0) - startProgress_) * value);
  });
  connect(&timeline_, &QTimeLine::finished, this, [this] { emit finished(targetOpen_); });
}
void ModalMotion::setProgress(qreal value) {
  progress_ = value;
  update();
  emit progressChanged(value);
}
void ModalMotion::reset() {
  timeline_.stop();
  setProgress(0);
}
void ModalMotion::animate(bool open) {
  timeline_.stop();
  targetOpen_ = open;
  if (qEnvironmentVariableIntValue("G2048_REDUCED_MOTION") == 1) {
    setProgress(open ? 1 : 0);
    emit finished(open);
    return;
  }
  startProgress_ = progress_;
  const auto *widget = qobject_cast<QWidget *>(parent());
  const auto *screen = widget ? widget->screen() : nullptr;
  const double refresh = screen && screen->refreshRate() > 0 ? screen->refreshRate() : 60;
  timeline_.setUpdateInterval(std::max(1, qRound(1000 / refresh)));
  timeline_.start();
}
void ModalMotion::draw(QPainter *p) {
  if (progress_ <= 0) return;
  if (progress_ >= 1) { drawSource(p); return; }
  QPoint offset;
  const auto pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
  const auto center = sourceBoundingRect(Qt::LogicalCoordinates).center();
  p->save();
  p->setRenderHint(QPainter::SmoothPixmapTransform);
  p->setOpacity(p->opacity() * progress_);
  p->translate(center); p->scale(scale(), scale()); p->translate(-center);
  p->drawPixmap(offset, pixmap);
  p->restore();
}
}
