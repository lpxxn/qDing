#include "ui/fireworks.h"
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <array>
#include <cmath>
#include <numbers>

namespace qding {
FireworksWidget::FireworksWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, [this] {
        seconds_ = elapsed_.elapsed() / 1000.0;
        if (seconds_ >= 4.2) {
            timer_.stop(); // 有限动画；长期等待用户处理时不持续占用 CPU。
            seconds_ = 0;
        }
        update();
    });
}
void FireworksWidget::setAnimationEnabled(bool enabled) {
    enabled_ = enabled;
    if (enabled && isVisible())
        start();
    else {
        timer_.stop();
        seconds_ = 0;
        update();
    }
}
void FireworksWidget::start() {
    if (!enabled_)
        return;
    seconds_ = 0;
    elapsed_.start();
    timer_.start();
}
void FireworksWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    start();
}
void FireworksWidget::hideEvent(QHideEvent *event) {
    timer_.stop();
    seconds_ = 0;
    QWidget::hideEvent(event);
}
void FireworksWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 16, 16);
    painter.setClipPath(clip);
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, QColor("#262b48"));
    background.setColorAt(0.55, QColor("#20263e"));
    background.setColorAt(1, QColor("#382f4a"));
    painter.fillRect(rect(), background);
    // 可重现的星光背景，无随机全局状态，也无需外部素材或着色器。
    for (int i = 0; i < 30; ++i) {
        const QPointF point(((i * 73 + 19) % 997) / 997.0 * width(),
                            ((i * 131 + 53) % 991) / 991.0 * height());
        QColor star("#e2deff");
        star.setAlphaF(0.2 + 0.12 * (1 + std::sin(seconds_ * 2 + i)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(star);
        painter.drawEllipse(point, i % 5 == 0 ? 1.5 : 0.8, i % 5 == 0 ? 1.5 : 0.8);
    }
    // 轻柔的中心光晕与钟面，关闭动画时仍有完整的静态装饰。
    const QPointF center(width() * 0.5, height() * 0.57);
    QRadialGradient halo(center, 78);
    halo.setColorAt(0, QColor(255, 184, 145, 42));
    halo.setColorAt(1, QColor(255, 184, 145, 0));
    painter.setBrush(halo);
    painter.drawEllipse(center, 78, 78);
    painter.setBrush(QColor("#fff0df"));
    painter.drawEllipse(center, 25, 25);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#e7856a"), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, 16, 16);
    painter.drawLine(center + QPointF(0, -9), center);
    painter.drawLine(center, center + QPointF(8, 5));
    if (!timer_.isActive())
        return;
    const std::array<QPointF, 5> origins{QPointF(0.19, 0.32), QPointF(0.78, 0.3),
                                         QPointF(0.47, 0.24), QPointF(0.3, 0.45),
                                         QPointF(0.85, 0.48)};
    const std::array<QColor, 5> colors{QColor("#ffd392"), QColor("#b7afff"), QColor("#ffa18a"),
                                       QColor("#93e4dc"), QColor("#ffc7a1")};
    painter.setCompositionMode(QPainter::CompositionMode_Screen);
    for (int burst = 0; burst < 5; ++burst) {
        const qreal age = seconds_ - (0.3 + burst * 0.46);
        const QPointF origin(origins[burst].x() * width(), origins[burst].y() * height());
        if (age >= -0.3 && age < 0) {
            const qreal progress = (age + 0.3) / 0.3;
            const QPointF head(origin.x(), height() + (origin.y() - height()) * progress);
            painter.setPen(QPen(colors[burst], 1.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(head, head + QPointF(0, 12));
            continue;
        }
        // 每次绽放最多 54 个粒子，总数上限 270；轨迹以秒为单位解析计算。
        for (int i = 0; i < 54; ++i) {
            const qreal life = 1.2 + (i % 7) * 0.08;
            if (age < 0 || age > life)
                continue;
            const qreal angle = i * 2 * std::numbers::pi / 54 + burst * 0.35;
            const qreal speed = 40 + (i * 17 % 41);
            const auto position = [&](qreal at) {
                const qreal radius = speed * (1 - std::exp(-1.8 * at));
                return origin +
                       QPointF(std::cos(angle) * radius, std::sin(angle) * radius + at * at * 18);
            };
            QColor color = colors[burst];
            color.setAlphaF(std::pow(1 - age / life, 1.3));
            painter.setPen(QPen(color, i % 3 == 0 ? 2.1 : 1.4, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(position(qMax(0.0, age - 0.055)), position(age));
        }
    }
}
} // namespace qding
