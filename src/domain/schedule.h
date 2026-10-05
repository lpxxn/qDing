#pragma once
#include "domain/types.h"
#include <optional>

namespace qding {
// 无 IO 的日历计算，now 参数可在测试中任意指定，无需等待真实时间。
class ScheduleCalculator final {
public:
    static std::optional<QDateTime> next(const ScheduleRule &rule, const QDateTime &after);
    static std::optional<QDateTime> latest(const ScheduleRule &rule, const QDateTime &after,
                                           const QDateTime &until);
    static QString occurrenceKey(const ScheduleRule &rule, const QDateTime &at);
    static bool sameSchedule(const ScheduleRule &a, const ScheduleRule &b);
    static QString validate(const Reminder &reminder);
};
} // namespace qding
