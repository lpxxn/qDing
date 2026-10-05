#include "ui/countdownring.h"
#include "services/theme.h"
#include <QPainter>
namespace qding {
CountdownRing::CountdownRing(QWidget *parent) : QWidget(parent) {
    setMinimumSize(280, 280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void CountdownRing::setTheme(ThemeManager *theme) {
    theme_ = theme;
    connect(theme_, &ThemeManager::changed, this, qOverload<>(&CountdownRing::update));
    update();
}
void CountdownRing::setSnapshot(PomodoroSnapshot state) {
    state_ = state;
    update();
}
void CountdownRing::paintEvent(QPaintEvent *) {
    if (!theme_)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &t = theme_->theme();
    const auto side = qMin(width(), height()) - 30;
    QRectF circle((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    p.setPen(QPen(t.border, 12, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(circle);
    const double total = qMax<qint64>(1, state_.phaseDurationMs);
    const double ratio = qBound(0.0, state_.remainingMs / total, 1.0);
    p.setPen(QPen(t.accent, 12, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(circle, 90 * 16, static_cast<int>(-ratio * 360 * 16));
    auto font = p.font();
    font.setPointSize(42);
    font.setBold(true);
    p.setFont(font);
    p.setPen(t.text);
    const auto seconds = (qMax<qint64>(0, state_.remainingMs) + 999) / 1000;
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("%1:%2")
                   .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                   .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    font.setPointSize(12);
    font.setBold(false);
    p.setFont(font);
    p.setPen(t.muted);
    p.drawText(rect().adjusted(0, 110, 0, 0), Qt::AlignCenter,
               state_.phase == PomodoroPhase::Work ? QStringLiteral("专注时间")
                                                   : QStringLiteral("休息时间"));
}
} // namespace qding
