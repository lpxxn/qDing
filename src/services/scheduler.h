#pragma once
#include "domain/types.h"
#include <QTimer>

namespace qding {
class StorageService;
class ReminderScheduler final : public QObject {
    Q_OBJECT
public:
    explicit ReminderScheduler(StorageService *storage, QObject *parent = nullptr);
    void setReminders(ReminderList reminders);
    void start();
    void stop();
    void wake();
    void setQuiet(bool quiet);
    void setGraceSeconds(int seconds);
    QDateTime nextReminder() const;
signals:
    void due(qding::OccurrenceList occurrences);
    void checked(QDateTime time);

private:
    void scan();
    void arm();
    QDateTime nextAfter(const QDateTime &after) const;
    StorageService *storage_;
    ReminderList reminders_;
    QTimer timer_;
    QDateTime nextDelivery_;
    QDateTime checkedAt_;
    bool running_ = false;
    bool busy_ = false;
    bool quiet_ = false;
    int graceSeconds_ = 600;
};
} // namespace qding
