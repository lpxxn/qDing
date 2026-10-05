#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QTime>
#include <QUuid>

namespace qding {

inline QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

enum class RuleKind { Once, Daily, Weekly };

// bit0=星期一，bit6=星期日；当地日历规则不能存成固定的 UTC 时刻。
struct ScheduleRule {
    QString id = newId();
    RuleKind kind = RuleKind::Daily;
    QTime time{10, 0};
    int weekdays = 127;
    QDateTime onceAt;
    QString timezone; // 空字符串表示跟随系统；非空为 IANA 时区 ID。
    QDateTime effectiveFrom = QDateTime::currentDateTimeUtc();
    int revision = 1;
};

struct Reminder {
    QString id = newId();
    QString title;
    QString body;
    bool enabled = true;
    QString visual; // 应用数据目录内的相对路径，内置资源可为空。
    QString sound;
    int volume = 70;
    QList<ScheduleRule> rules;
};
using ReminderList = QList<Reminder>;

// occurrence 是一次计划发生，稍后提醒仍使用同一 id，避免改动下一天的规则。
struct Occurrence {
    QString id;
    QString reminderId;
    QString ruleId;
    QString title;
    QString body;
    QString visual;
    QString sound;
    int volume = 70;
    QDateTime scheduledAt;
    QDateTime deliveryAt;
    QString status;
};
using OccurrenceList = QList<Occurrence>;

enum class PomodoroPhase { Work, Break };
enum class PomodoroStatus { Idle, Running, Paused };

struct PomodoroSnapshot {
    PomodoroPhase phase = PomodoroPhase::Work;
    PomodoroStatus status = PomodoroStatus::Idle;
    qint64 remainingMs = 25 * 60 * 1000;
    qint64 phaseDurationMs = 25 * 60 * 1000;
    int workMinutes = 25;
    int breakMinutes = 5;
    int rounds = 0;
};

inline QString ruleSummary(const ScheduleRule &rule) {
    if (rule.kind == RuleKind::Once)
        return rule.onceAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QString days;
    if (rule.kind == RuleKind::Daily || rule.weekdays == 127)
        days = QStringLiteral("每天");
    else if (rule.weekdays == 31)
        days = QStringLiteral("周一至周五");
    else {
        const QStringList labels{QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"),
                                 QStringLiteral("四"), QStringLiteral("五"), QStringLiteral("六"),
                                 QStringLiteral("日")};
        QStringList selected;
        for (int i = 0; i < 7; ++i)
            if (rule.weekdays & (1 << i))
                selected << labels[i];
        days = QStringLiteral("周") + selected.join(QStringLiteral("、"));
    }
    return days + QLatin1Char(' ') + rule.time.toString(QStringLiteral("HH:mm"));
}

} // namespace qding

Q_DECLARE_METATYPE(qding::Reminder)
Q_DECLARE_METATYPE(qding::ReminderList)
Q_DECLARE_METATYPE(qding::Occurrence)
Q_DECLARE_METATYPE(qding::OccurrenceList)
Q_DECLARE_METATYPE(qding::PomodoroSnapshot)
