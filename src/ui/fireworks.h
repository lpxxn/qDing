#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace qding {
// Designer 提升控件。粒子位置由单调时间计算，不依赖定时器回调是否准时。
class FireworksWidget final : public QWidget {
    Q_OBJECT
public:
    explicit FireworksWidget(QWidget *parent = nullptr);
    void setAnimationEnabled(bool enabled);
    bool isAnimating() const { return timer_.isActive(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void start();
    QTimer timer_;
    QElapsedTimer elapsed_;
    qreal seconds_ = 0;
    bool enabled_ = true;
};
} // namespace qding
