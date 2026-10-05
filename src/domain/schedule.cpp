#include "domain/schedule.h"
#include <QTimeZone>

namespace qding {
namespace {
QTimeZone zoneFor(const ScheduleRule &rule) {
    return rule.timezone.isEmpty() ? QTimeZone::systemTimeZone()
                                   : QTimeZone(rule.timezone.toUtf8());
}
std::optional<QDateTime> candidate(const ScheduleRule &rule, const QDate &date) {
    if (rule.kind == RuleKind::Weekly && !(rule.weekdays & (1 << (date.dayOfWeek() - 1))))
        return std::nullopt;
    const auto zone = zoneFor(rule);
    if (!zone.isValid())
        return std::nullopt;
    // PreferBefore 选择回拨时第一次出现；正拨导致时间被调整时，通过回查拒绝。
    const QDateTime local(date, rule.time, zone, QDateTime::TransitionResolution::PreferBefore);
    if (!local.isValid() || local.date() != date || local.time() != rule.time)
        return std::nullopt;
    return local.toUTC();
}
} // namespace

std::optional<QDateTime> ScheduleCalculator::next(const ScheduleRule &rule,
                                                  const QDateTime &after) {
    if (!after.isValid())
        return std::nullopt;
    if (rule.kind == RuleKind::Once) {
        if (rule.onceAt.isValid() && rule.onceAt > after && rule.onceAt >= rule.effectiveFrom)
            return rule.onceAt.toUTC();
        return std::nullopt;
    }
    if (!rule.time.isValid() || rule.weekdays < 1 || rule.weekdays > 127)
        return std::nullopt;
    const auto lower = qMax(after, rule.effectiveFrom.addMSecs(-1));
    const auto date = lower.toTimeZone(zoneFor(rule)).date();
    for (int i = 0; i < 15; ++i) {
        const auto at = candidate(rule, date.addDays(i));
        if (at && *at > after && *at >= rule.effectiveFrom)
            return at;
    }
    return std::nullopt;
}

std::optional<QDateTime> ScheduleCalculator::latest(const ScheduleRule &rule,
                                                    const QDateTime &after,
                                                    const QDateTime &until) {
    if (!until.isValid() || until <= after)
        return std::nullopt;
    if (rule.kind == RuleKind::Once) {
        if (rule.onceAt.isValid() && rule.onceAt > after && rule.onceAt <= until &&
            rule.onceAt >= rule.effectiveFrom)
            return rule.onceAt.toUTC();
        return std::nullopt;
    }
    if (!rule.time.isValid() || rule.weekdays < 1 || rule.weekdays > 127)
        return std::nullopt;
    const auto date = until.toTimeZone(zoneFor(rule)).date();
    // 只找最近一次，长时间未运行不会枚举多年的提醒并造成弹窗风暴。
    for (int i = 0; i < 15; ++i) {
        const auto at = candidate(rule, date.addDays(-i));
        if (at && *at <= until && *at > after && *at >= rule.effectiveFrom)
            return at;
    }
    return std::nullopt;
}

QString ScheduleCalculator::occurrenceKey(const ScheduleRule &rule, const QDateTime &at) {
    const auto time =
        rule.kind == RuleKind::Once
            ? QString::number(at.toMSecsSinceEpoch())
            : at.toTimeZone(zoneFor(rule)).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    return rule.id + QLatin1Char(':') + QString::number(rule.revision) + QLatin1Char(':') + time;
}

bool ScheduleCalculator::sameSchedule(const ScheduleRule &a, const ScheduleRule &b) {
    return a.kind == b.kind && a.time == b.time && a.weekdays == b.weekdays &&
           a.onceAt == b.onceAt && a.timezone == b.timezone;
}

QString ScheduleCalculator::validate(const Reminder &r) {
    if (r.title.trimmed().isEmpty())
        return QStringLiteral("请输入提醒标题。");
    if (r.title.size() > 200 || r.body.size() > 10000)
        return QStringLiteral("标题或正文过长。");
    if (r.volume < 0 || r.volume > 100)
        return QStringLiteral("音量必须在 0–100 之间。");
    if (r.rules.isEmpty() || r.rules.size() > 32)
        return QStringLiteral("请设置 1–32 条时间规则。");
    for (const auto &rule : r.rules) {
        if (!zoneFor(rule).isValid())
            return QStringLiteral("时区无效。");
        if (rule.kind == RuleKind::Once && !rule.onceAt.isValid())
            return QStringLiteral("日期时间无效。");
        if (rule.kind != RuleKind::Once &&
            (!rule.time.isValid() || rule.weekdays < 1 || rule.weekdays > 127))
            return QStringLiteral("请选择有效时间和至少一个星期。");
    }
    return {};
}
} // namespace qding
