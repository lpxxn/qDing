#include "services/scheduler.h"
#include "domain/schedule.h"
#include "storage/storage.h"
#include <spdlog/spdlog.h>

namespace qding {
ReminderScheduler::ReminderScheduler(StorageService *storage, QObject *parent)
    : QObject(parent), storage_(storage) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &ReminderScheduler::scan);
    connect(storage_, &StorageService::scanFinished, this,
            [this](OccurrenceList list, QDateTime at, QDateTime next) {
                busy_ = false;
                nextDelivery_ = next;
                checkedAt_ = at;
                emit checked(at);
                if (running_)
                    emit due(list);
                arm();
            });
    connect(storage_, &StorageService::error, this, [this](const QString &) {
        if (busy_) {
            busy_ = false;
            if (running_)
                timer_.start(5000);
        }
    });
    connect(storage_, &StorageService::occurrenceChanged, this, [this](const Occurrence &o) {
        if (o.status == QLatin1String("snoozed"))
            nextDelivery_ = o.deliveryAt;
        wake();
    });
}
void ReminderScheduler::setReminders(ReminderList list) {
    reminders_ = std::move(list);
    wake();
}
void ReminderScheduler::start() {
    running_ = true;
    wake();
}
void ReminderScheduler::stop() {
    running_ = false;
    timer_.stop();
}
void ReminderScheduler::wake() {
    if (running_ && !busy_)
        timer_.start(0);
}
void ReminderScheduler::setQuiet(bool value) {
    if (quiet_ == value)
        return;
    quiet_ = value;
    wake();
}
void ReminderScheduler::setGraceSeconds(int value) {
    graceSeconds_ = qBound(0, value, 86400);
    wake();
}
QDateTime ReminderScheduler::nextReminder() const {
    return nextAfter(QDateTime::currentDateTimeUtc());
}
QDateTime ReminderScheduler::nextAfter(const QDateTime &after) const {
    QDateTime closest;
    for (const auto &r : reminders_)
        if (r.enabled)
            for (const auto &rule : r.rules) {
                const auto at = ScheduleCalculator::next(rule, after);
                if (at && (!closest.isValid() || *at < closest))
                    closest = *at;
            }
    return closest;
}
void ReminderScheduler::scan() {
    if (!running_ || busy_)
        return;
    busy_ = true;
    storage_->scan(QDateTime::currentDateTimeUtc(), quiet_, graceSeconds_);
}
void ReminderScheduler::arm() {
    if (!running_ || busy_)
        return;
    const auto now = QDateTime::currentDateTimeUtc();
    qint64 wait = 30000; // 低频检查兜底检测墙上时间变化。
    // 查询截至 checkedAt_；GUI 接收结果时可能已经跨过到期时刻。
    // 仍以扫描边界找下一次计划，已到期则尽快再扫描，不能直接跳到 30 秒兜底。
    const auto next = nextAfter(checkedAt_.isValid() ? qMin(checkedAt_, now) : now);
    if (next.isValid())
        wait = qMin(wait, now.msecsTo(next));
    if (!quiet_ && nextDelivery_.isValid())
        wait = qMin(wait, now.msecsTo(nextDelivery_));
    timer_.start(static_cast<int>(qBound<qint64>(100LL, wait, 30000LL)));
}
} // namespace qding
