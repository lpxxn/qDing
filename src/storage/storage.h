#pragma once
#include "domain/types.h"
#include <QSqlDatabase>
#include <QThread>

namespace qding {

// Worker 只在存储线程使用。GUI 不持有 QSqlDatabase/QSqlQuery。
class DatabaseWorker final : public QObject {
    Q_OBJECT
public:
    explicit DatabaseWorker(QString databasePath);
    void initialize();
    void close();
    void loadReminders();
    void saveReminder(Reminder reminder);
    void removeReminder(const QString &id);
    void scan(QDateTime now, bool quiet, int graceSeconds);
    void handleOccurrence(const QString &id, const QString &status, QDateTime nextDelivery = {});
    void loadHistory();
    void savePomodoro(PomodoroSnapshot snapshot);
signals:
    void ready(qding::ReminderList reminders, qding::PomodoroSnapshot snapshot);
    void remindersLoaded(qding::ReminderList reminders);
    void reminderSaved(qding::Reminder reminder);
    void reminderRemoved(QString id);
    void scanFinished(qding::OccurrenceList occurrences, QDateTime checkedAt,
                      QDateTime nextDelivery);
    void occurrenceChanged(qding::Occurrence occurrence);
    void historyLoaded(qding::OccurrenceList occurrences);
    void error(QString message);

private:
    ReminderList readReminders();
    PomodoroSnapshot readPomodoro();
    bool begin();
    void reportFailure(const std::exception &error);
    QString path_;
    QString connectionName_;
    QSqlDatabase db_;
};

// Facade 属于 GUI 线程；所有调用以 queued invocation 发往 Worker，结果也是 queued signals。
class StorageService final : public QObject {
    Q_OBJECT
public:
    explicit StorageService(QString path, QObject *parent = nullptr);
    ~StorageService() override;
    void start();
    void shutdown();
    void loadReminders();
    void saveReminder(Reminder reminder);
    void removeReminder(QString id);
    void scan(QDateTime now, bool quiet, int graceSeconds);
    void handleOccurrence(QString id, QString status, QDateTime nextDelivery = {});
    void loadHistory();
    void savePomodoro(PomodoroSnapshot snapshot);
signals:
    void ready(qding::ReminderList reminders, qding::PomodoroSnapshot snapshot);
    void remindersLoaded(qding::ReminderList reminders);
    void reminderSaved(qding::Reminder reminder);
    void reminderRemoved(QString id);
    void scanFinished(qding::OccurrenceList occurrences, QDateTime checkedAt,
                      QDateTime nextDelivery);
    void occurrenceChanged(qding::Occurrence occurrence);
    void historyLoaded(qding::OccurrenceList occurrences);
    void error(QString message);

private:
    QThread thread_;
    DatabaseWorker *worker_;
    bool started_ = false;
};
} // namespace qding
