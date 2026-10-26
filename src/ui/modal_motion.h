#pragma once
#include <QGraphicsEffect>
#include <QTimeLine>
namespace g2048 {
class ModalMotion : public QGraphicsEffect {
  Q_OBJECT
public:
  static constexpr int duration = 120;
  explicit ModalMotion(QObject *parent = nullptr);
  qreal progress() const { return progress_; }
  qreal scale() const { return .98 + .02 * progress_; }
  bool isRunning() const { return timeline_.state() == QTimeLine::Running; }
  void animate(bool open);
  void reset();
signals:
  void progressChanged(qreal progress);
  void finished(bool open);
protected:
  void draw(QPainter *painter) override;
private:
  QTimeLine timeline_;
  qreal startProgress_ = 0;
  qreal progress_ = 0;
  bool targetOpen_ = false;
  void setProgress(qreal value);
};
}
