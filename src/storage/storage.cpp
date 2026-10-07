#include "storage/storage.h"
#include "domain/schedule.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace qding {
namespace {
void checked(QSqlQuery &q) {
    if (!q.exec())
        throw std::runtime_error(q.lastError().text().toStdString());
}
void execute(QSqlDatabase &db, const QString &sql) {
    QSqlQuery q(db);
    if (!q.exec(sql))
        throw std::runtime_error(q.lastError().text().toStdString());
}
QDateTime timeFrom(const QVariant &value) {
    return value.isNull() ? QDateTime{}
                          : QDateTime::fromMSecsSinceEpoch(value.toLongLong(), QTimeZone::UTC);
}
// 固定列顺序集中定义，历史和待展示查询使用相同映射。
const QString occurrenceColumns = QStringLiteral(
    "id,reminder_id,rule_id,title,body,visual,sound,volume,scheduled_at,delivery_at,status");
Occurrence readOccurrence(const QSqlQuery &q) {
    Occurrence o;
    o.id = q.value(0).toString();
    o.reminderId = q.value(1).toString();
    o.ruleId = q.value(2).toString();
    o.title = q.value(3).toString();
    o.body = q.value(4).toString();
    o.visual = q.value(5).toString();
    o.sound = q.value(6).toString();
    o.volume = q.value(7).toInt();
    o.scheduledAt = timeFrom(q.value(8));
    o.deliveryAt = timeFrom(q.value(9));
    o.status = q.value(10).toString();
    return o;
}
} // namespace

DatabaseWorker::DatabaseWorker(QString path)
    : path_(std::move(path)), connectionName_(QStringLiteral("qding-") + newId()) {}

void DatabaseWorker::initialize() {
    try {
        if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
            throw std::runtime_error("Cannot create data directory");
        // "QSQLITE" 由 sqldrivers 插件（qsqlite）实现；发布包须带上该插件，见 docs/qt-plugins.zh-CN.md。
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
        db_.setDatabaseName(path_);
        db_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
        if (!db_.open())
            throw std::runtime_error(db_.lastError().text().toStdString());
        execute(db_, QStringLiteral("PRAGMA foreign_keys=ON"));
        QSqlQuery version(db_);
        if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next())
            throw std::runtime_error("Cannot read schema version");
        const int schema = version.value(0).toInt();
        version.finish();
        if (schema > 1)
            throw std::runtime_error("Database was created by a newer qDing version");
        if (schema == 0) {
            if (!db_.transaction())
                throw std::runtime_error(db_.lastError().text().toStdString());
            execute(db_, QStringLiteral("CREATE TABLE reminders(id TEXT PRIMARY KEY,title TEXT NOT "
                                        "NULL,body TEXT NOT NULL,"
                                        "enabled INTEGER NOT NULL CHECK(enabled IN(0,1)),visual "
                                        "TEXT NOT NULL,sound TEXT NOT NULL,"
                                        "volume INTEGER NOT NULL CHECK(volume BETWEEN 0 AND "
                                        "100),deleted INTEGER NOT NULL DEFAULT 0)"));
            execute(db_, QStringLiteral("CREATE TABLE rules(id TEXT PRIMARY KEY,reminder_id TEXT "
                                        "NOT NULL REFERENCES reminders(id),"
                                        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND "
                                        "2),local_time TEXT NOT NULL,weekdays INTEGER NOT NULL,"
                                        "once_at INTEGER,timezone TEXT NOT NULL,effective_from "
                                        "INTEGER NOT NULL,revision INTEGER NOT NULL)"));
            execute(db_, QStringLiteral(
                             "CREATE TABLE occurrences(id TEXT PRIMARY KEY,reminder_id TEXT NOT "
                             "NULL REFERENCES reminders(id),"
                             "rule_id TEXT NOT NULL,occurrence_key TEXT NOT NULL UNIQUE,title TEXT "
                             "NOT NULL,body TEXT NOT NULL,"
                             "visual TEXT NOT NULL,sound TEXT NOT NULL,volume INTEGER NOT "
                             "NULL,scheduled_at INTEGER NOT NULL,"
                             "delivery_at INTEGER NOT NULL,status TEXT NOT NULL CHECK(status IN"
                             "('pending','claimed','shown','snoozed','deferred','completed','"
                             "dismissed','missed','interrupted','cancelled')),"
                             "shown_at INTEGER,handled_at INTEGER)"));
            execute(db_, QStringLiteral(
                             "CREATE INDEX occurrence_due ON occurrences(status,delivery_at)"));
            execute(db_, QStringLiteral(
                             "CREATE INDEX occurrence_history ON occurrences(scheduled_at DESC)"));
            execute(db_, QStringLiteral("CREATE INDEX rule_reminder ON rules(reminder_id)"));
            execute(db_, QStringLiteral(
                             "CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"));
            execute(db_, QStringLiteral("CREATE TABLE pomodoro(id INTEGER PRIMARY KEY "
                                        "CHECK(id=1),phase INTEGER NOT NULL,"
                                        "status INTEGER NOT NULL,remaining_ms INTEGER NOT "
                                        "NULL,work_minutes INTEGER NOT NULL,"
                                        "break_minutes INTEGER NOT NULL,rounds INTEGER NOT NULL)"));
            execute(db_, QStringLiteral("PRAGMA user_version=1"));
            if (!db_.commit())
                throw std::runtime_error(db_.lastError().text().toStdString());
        }
        // 屏幕展示和 DB 更新不能原子提交；孤立领取记录明确标为 interrupted。
        execute(
            db_,
            QStringLiteral(
                "UPDATE occurrences SET status='interrupted' WHERE status IN('claimed','shown')"));
        spdlog::info("storage initialized schema=1");
        emit ready(readReminders(), readPomodoro());
    } catch (const std::exception &e) {
        reportFailure(e);
        // 尤其是数据库版本过新时，不能让退出快照等后续任务继续写这个连接。
        close();
    }
}

void DatabaseWorker::close() {
    if (!db_.isValid())
        return;
    db_.close();
    db_ = QSqlDatabase(); // 清除连接引用之后再 removeDatabase。
    QSqlDatabase::removeDatabase(connectionName_);
}

bool DatabaseWorker::begin() {
    if (!db_.isOpen() || !db_.transaction())
        throw std::runtime_error("Database is not available for a transaction");
    return true;
}

void DatabaseWorker::reportFailure(const std::exception &e) {
    if (db_.isOpen())
        db_.rollback();
    spdlog::error("storage failure: {}", e.what());
    emit error(QStringLiteral("数据库操作失败：") + QString::fromUtf8(e.what()));
}

ReminderList DatabaseWorker::readReminders() {
    ReminderList list;
    QHash<QString, qsizetype> positions;
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT id,title,body,enabled,visual,sound,volume FROM reminders "
                               "WHERE deleted=0 ORDER BY rowid DESC")))
        throw std::runtime_error(q.lastError().text().toStdString());
    while (q.next()) {
        Reminder r;
        r.id = q.value(0).toString();
        r.title = q.value(1).toString();
        r.body = q.value(2).toString();
        r.enabled = q.value(3).toBool();
        r.visual = q.value(4).toString();
        r.sound = q.value(5).toString();
        r.volume = q.value(6).toInt();
        positions.insert(r.id, list.size());
        list.append(r);
    }
    if (!q.exec(QStringLiteral("SELECT "
                               "id,reminder_id,kind,local_time,weekdays,once_at,timezone,effective_"
                               "from,revision FROM rules ORDER BY rowid")))
        throw std::runtime_error(q.lastError().text().toStdString());
    while (q.next()) {
        const auto id = q.value(1).toString();
        if (!positions.contains(id))
            continue;
        ScheduleRule rule;
        rule.id = q.value(0).toString();
        rule.kind = static_cast<RuleKind>(q.value(2).toInt());
        rule.time = QTime::fromString(q.value(3).toString(), QStringLiteral("HH:mm:ss"));
        rule.weekdays = q.value(4).toInt();
        rule.onceAt = timeFrom(q.value(5));
        rule.timezone = q.value(6).toString();
        rule.effectiveFrom = timeFrom(q.value(7));
        rule.revision = q.value(8).toInt();
        list[positions[id]].rules.append(rule);
    }
    return list;
}

void DatabaseWorker::loadReminders() {
    try {
        emit remindersLoaded(readReminders());
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

void DatabaseWorker::saveReminder(Reminder r) {
    try {
        const auto invalid = ScheduleCalculator::validate(r);
        if (!invalid.isEmpty()) {
            emit error(invalid);
            return;
        }
        const auto existing = readReminders();
        const auto now = QDateTime::currentDateTimeUtc();
        for (auto &rule : r.rules) {
            bool found = false;
            for (const auto &old : existing)
                if (old.id == r.id)
                    for (const auto &oldRule : old.rules) {
                        if (oldRule.id != rule.id)
                            continue;
                        found = true;
                        rule.revision = oldRule.revision;
                        rule.effectiveFrom = oldRule.effectiveFrom;
                        if (!ScheduleCalculator::sameSchedule(rule, oldRule) ||
                            (!old.enabled && r.enabled)) {
                            ++rule.revision;
                            rule.effectiveFrom = now;
                        }
                    }
            if (!found) {
                rule.revision = 1;
                rule.effectiveFrom = now;
            }
        }
        begin();
        QSqlQuery q(db_);
        q.prepare(QStringLiteral(
            "INSERT INTO reminders(id,title,body,enabled,visual,sound,volume) "
            "VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET "
            "title=excluded.title,body=excluded.body,enabled=excluded.enabled,"
            "visual=excluded.visual,sound=excluded.sound,volume=excluded.volume,deleted=0"));
        q.addBindValue(r.id);
        q.addBindValue(r.title.trimmed());
        q.addBindValue(r.body.isNull() ? QStringLiteral("") : r.body);
        q.addBindValue(r.enabled);
        q.addBindValue(r.visual.isNull() ? QStringLiteral("") : r.visual);
        q.addBindValue(r.sound.isNull() ? QStringLiteral("") : r.sound);
        q.addBindValue(r.volume);
        checked(q);
        // 编辑时取消旧展示/稍后请求，避免旧内容在编辑后继续弹出。
        q.prepare(
            QStringLiteral("UPDATE occurrences SET status='cancelled' WHERE reminder_id=? AND "
                           "status IN('pending','claimed','shown','snoozed','deferred')"));
        q.addBindValue(r.id);
        checked(q);
        q.prepare(QStringLiteral("DELETE FROM rules WHERE reminder_id=?"));
        q.addBindValue(r.id);
        checked(q);
        for (const auto &rule : r.rules) {
            q.prepare(QStringLiteral("INSERT INTO rules VALUES(?,?,?,?,?,?,?,?,?)"));
            q.addBindValue(rule.id);
            q.addBindValue(r.id);
            q.addBindValue(static_cast<int>(rule.kind));
            q.addBindValue(rule.time.toString(QStringLiteral("HH:mm:ss")));
            q.addBindValue(rule.weekdays);
            q.addBindValue(rule.onceAt.isValid() ? QVariant(rule.onceAt.toMSecsSinceEpoch())
                                                 : QVariant{});
            q.addBindValue(rule.timezone.isNull() ? QStringLiteral("") : rule.timezone);
            q.addBindValue(rule.effectiveFrom.toMSecsSinceEpoch());
            q.addBindValue(rule.revision);
            checked(q);
        }
        if (!db_.commit())
            throw std::runtime_error(db_.lastError().text().toStdString());
        spdlog::info("reminder saved id={} rules={} enabled={}", r.id.toStdString(), r.rules.size(),
                     r.enabled);
        emit reminderSaved(r);
        emit remindersLoaded(readReminders());
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

void DatabaseWorker::removeReminder(const QString &id) {
    try {
        begin();
        QSqlQuery q(db_);
        q.prepare(QStringLiteral("UPDATE reminders SET deleted=1,enabled=0 WHERE id=?"));
        q.addBindValue(id);
        checked(q);
        q.prepare(
            QStringLiteral("UPDATE occurrences SET status='cancelled' WHERE reminder_id=? AND "
                           "status IN('pending','claimed','shown','snoozed','deferred')"));
        q.addBindValue(id);
        checked(q);
        if (!db_.commit())
            throw std::runtime_error(db_.lastError().text().toStdString());
        spdlog::info("reminder removed id={}", id.toStdString());
        emit reminderRemoved(id);
        emit remindersLoaded(readReminders());
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

void DatabaseWorker::scan(QDateTime now, bool quiet, int graceSeconds) {
    try {
        QSqlQuery q(db_);
        q.prepare(QStringLiteral("SELECT value FROM metadata WHERE key='last_scan'"));
        checked(q);
        QDateTime from = now.addMSecs(-1);
        if (q.next())
            from = QDateTime::fromMSecsSinceEpoch(q.value(0).toLongLong(), QTimeZone::UTC);
        q.finish();
        if (from > now)
            from = now.addMSecs(-1); // 系统回拨后重新建立检查下界，唯一键负责去重。
        const auto reminders = readReminders();
        begin();
        for (const auto &r : reminders) {
            if (!r.enabled)
                continue;
            for (const auto &rule : r.rules) {
                const auto at = ScheduleCalculator::latest(rule, from, now);
                if (!at)
                    continue;
                const auto status = at->secsTo(now) > graceSeconds ? QStringLiteral("missed")
                                    : quiet                        ? QStringLiteral("deferred")
                                                                   : QStringLiteral("pending");
                q.prepare(QStringLiteral(
                    "INSERT INTO "
                    "occurrences(id,reminder_id,rule_id,occurrence_key,title,body,visual,sound,"
                    "volume,scheduled_at,delivery_at,status) VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON "
                    "CONFLICT(occurrence_key) DO NOTHING"));
                q.addBindValue(newId());
                q.addBindValue(r.id);
                q.addBindValue(rule.id);
                q.addBindValue(ScheduleCalculator::occurrenceKey(rule, *at));
                q.addBindValue(r.title);
                q.addBindValue(r.body);
                q.addBindValue(r.visual.isNull() ? QStringLiteral("") : r.visual);
                q.addBindValue(r.sound.isNull() ? QStringLiteral("") : r.sound);
                q.addBindValue(r.volume);
                q.addBindValue(at->toMSecsSinceEpoch());
                q.addBindValue(at->toMSecsSinceEpoch());
                q.addBindValue(status);
                checked(q);
                spdlog::debug("schedule occurrence rule={} status={}", rule.id.toStdString(),
                              status.toStdString());
            }
        }
        q.prepare(QStringLiteral("INSERT INTO metadata VALUES('last_scan',?) ON CONFLICT(key) DO "
                                 "UPDATE SET value=excluded.value"));
        q.addBindValue(QString::number(now.toMSecsSinceEpoch()));
        checked(q);
        OccurrenceList ready;
        if (!quiet) {
            q.prepare(
                QStringLiteral("SELECT ") + occurrenceColumns +
                QStringLiteral(" FROM occurrences WHERE status IN('pending','snoozed','deferred') "
                               "AND delivery_at<=? ORDER BY delivery_at LIMIT 64"));
            q.addBindValue(now.toMSecsSinceEpoch());
            checked(q);
            OccurrenceList due;
            while (q.next())
                due.append(readOccurrence(q));
            q.finish();
            for (auto o : due) {
                o.status = o.deliveryAt.secsTo(now) > graceSeconds ? QStringLiteral("missed")
                                                                   : QStringLiteral("claimed");
                q.prepare(QStringLiteral("UPDATE occurrences SET status=? WHERE id=? AND status "
                                         "IN('pending','snoozed','deferred')"));
                q.addBindValue(o.status);
                q.addBindValue(o.id);
                checked(q);
                if (o.status == QLatin1String("claimed") && q.numRowsAffected() == 1)
                    ready.append(o);
            }
        }
        QDateTime nextDelivery;
        q.prepare(QStringLiteral("SELECT MIN(delivery_at) FROM occurrences WHERE status "
                                 "IN('pending','snoozed','deferred')"));
        checked(q);
        if (q.next())
            nextDelivery = timeFrom(q.value(0));
        q.finish();
        if (!db_.commit())
            throw std::runtime_error(db_.lastError().text().toStdString());
        emit scanFinished(ready, now, nextDelivery);
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

void DatabaseWorker::handleOccurrence(const QString &id, const QString &status,
                                      QDateTime nextDelivery) {
    try {
        const QStringList allowed{QStringLiteral("shown"), QStringLiteral("completed"),
                                  QStringLiteral("dismissed"), QStringLiteral("snoozed"),
                                  QStringLiteral("deferred")};
        if (!allowed.contains(status))
            throw std::runtime_error("Invalid occurrence transition");
        QSqlQuery q(db_);
        QString sql = QStringLiteral("UPDATE occurrences SET status=?,handled_at=?");
        if (nextDelivery.isValid())
            sql += QStringLiteral(",delivery_at=?");
        if (status == QLatin1String("shown"))
            sql += QStringLiteral(",shown_at=?");
        sql += QStringLiteral(" WHERE id=? AND status IN('claimed','shown')");
        q.prepare(sql);
        q.addBindValue(status);
        q.addBindValue(QDateTime::currentMSecsSinceEpoch());
        if (nextDelivery.isValid())
            q.addBindValue(nextDelivery.toMSecsSinceEpoch());
        if (status == QLatin1String("shown"))
            q.addBindValue(QDateTime::currentMSecsSinceEpoch());
        q.addBindValue(id);
        checked(q);
        const bool changed = q.numRowsAffected() == 1;
        if (!changed)
            return;
        q.prepare(QStringLiteral("SELECT ") + occurrenceColumns +
                  QStringLiteral(" FROM occurrences WHERE id=?"));
        q.addBindValue(id);
        checked(q);
        if (q.next())
            emit occurrenceChanged(readOccurrence(q));
        spdlog::info("occurrence handled id={} status={}", id.toStdString(), status.toStdString());
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

void DatabaseWorker::loadHistory() {
    try {
        QSqlQuery q(db_);
        if (!q.exec(QStringLiteral("SELECT ") + occurrenceColumns +
                    QStringLiteral(" FROM occurrences ORDER BY scheduled_at DESC LIMIT 300")))
            throw std::runtime_error(q.lastError().text().toStdString());
        OccurrenceList rows;
        while (q.next())
            rows.append(readOccurrence(q));
        emit historyLoaded(rows);
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

PomodoroSnapshot DatabaseWorker::readPomodoro() {
    PomodoroSnapshot s;
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT phase,status,remaining_ms,work_minutes,break_minutes,rounds "
                               "FROM pomodoro WHERE id=1")))
        throw std::runtime_error(q.lastError().text().toStdString());
    if (q.next()) {
        s.phase = static_cast<PomodoroPhase>(q.value(0).toInt());
        s.status = static_cast<PomodoroStatus>(q.value(1).toInt());
        s.remainingMs = q.value(2).toLongLong();
        s.workMinutes = q.value(3).toInt();
        s.breakMinutes = q.value(4).toInt();
        s.rounds = q.value(5).toInt();
    }
    s.phaseDurationMs = (s.phase == PomodoroPhase::Work ? s.workMinutes : s.breakMinutes) * 60000LL;
    q.finish();
    q.prepare(QStringLiteral("SELECT value FROM metadata WHERE key='pomodoro_phase_duration'"));
    checked(q);
    if (q.next())
        s.phaseDurationMs = q.value(0).toLongLong();
    return s;
}

void DatabaseWorker::savePomodoro(PomodoroSnapshot s) {
    try {
        begin();
        QSqlQuery q(db_);
        q.prepare(QStringLiteral(
            "INSERT INTO pomodoro VALUES(1,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
            "phase=excluded.phase,status=excluded.status,remaining_ms=excluded.remaining_ms,work_"
            "minutes=excluded.work_minutes,"
            "break_minutes=excluded.break_minutes,rounds=excluded.rounds"));
        q.addBindValue(static_cast<int>(s.phase));
        q.addBindValue(static_cast<int>(s.status));
        q.addBindValue(s.remainingMs);
        q.addBindValue(s.workMinutes);
        q.addBindValue(s.breakMinutes);
        q.addBindValue(s.rounds);
        checked(q);
        q.prepare(QStringLiteral("INSERT INTO metadata VALUES('pomodoro_phase_duration',?) ON "
                                 "CONFLICT(key) DO UPDATE SET value=excluded.value"));
        q.addBindValue(QString::number(s.phaseDurationMs));
        checked(q);
        if (!db_.commit())
            throw std::runtime_error(db_.lastError().text().toStdString());
    } catch (const std::exception &e) {
        reportFailure(e);
    }
}

StorageService::StorageService(QString path, QObject *parent)
    : QObject(parent), worker_(new DatabaseWorker(std::move(path))) {
    qRegisterMetaType<Reminder>();
    qRegisterMetaType<ReminderList>();
    qRegisterMetaType<Occurrence>();
    qRegisterMetaType<OccurrenceList>();
    qRegisterMetaType<PomodoroSnapshot>();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::started, worker_, &DatabaseWorker::initialize);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &DatabaseWorker::ready, this, &StorageService::ready);
    connect(worker_, &DatabaseWorker::remindersLoaded, this, &StorageService::remindersLoaded);
    connect(worker_, &DatabaseWorker::reminderSaved, this, &StorageService::reminderSaved);
    connect(worker_, &DatabaseWorker::reminderRemoved, this, &StorageService::reminderRemoved);
    connect(worker_, &DatabaseWorker::scanFinished, this, &StorageService::scanFinished);
    connect(worker_, &DatabaseWorker::occurrenceChanged, this, &StorageService::occurrenceChanged);
    connect(worker_, &DatabaseWorker::historyLoaded, this, &StorageService::historyLoaded);
    connect(worker_, &DatabaseWorker::error, this, &StorageService::error);
}
StorageService::~StorageService() {
    shutdown();
}
void StorageService::start() {
    if (!started_) {
        started_ = true;
        thread_.start();
    }
}
void StorageService::shutdown() {
    if (!started_) {
        delete worker_;
        worker_ = nullptr;
        return;
    }
    if (!thread_.isRunning())
        return;
    // 同一队列先完成已提交写入，再关闭；只在正常退出阶段短暂等待。
    QMetaObject::invokeMethod(worker_, [this] { worker_->close(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}
void StorageService::loadReminders() {
    QMetaObject::invokeMethod(worker_, [this] { worker_->loadReminders(); }, Qt::QueuedConnection);
}
void StorageService::saveReminder(Reminder r) {
    QMetaObject::invokeMethod(
        worker_, [this, r] { worker_->saveReminder(r); }, Qt::QueuedConnection);
}
void StorageService::removeReminder(QString id) {
    QMetaObject::invokeMethod(
        worker_, [this, id] { worker_->removeReminder(id); }, Qt::QueuedConnection);
}
void StorageService::scan(QDateTime now, bool quiet, int grace) {
    QMetaObject::invokeMethod(
        worker_, [this, now, quiet, grace] { worker_->scan(now, quiet, grace); },
        Qt::QueuedConnection);
}
void StorageService::handleOccurrence(QString id, QString status, QDateTime next) {
    QMetaObject::invokeMethod(
        worker_, [this, id, status, next] { worker_->handleOccurrence(id, status, next); },
        Qt::QueuedConnection);
}
void StorageService::loadHistory() {
    QMetaObject::invokeMethod(worker_, [this] { worker_->loadHistory(); }, Qt::QueuedConnection);
}
void StorageService::savePomodoro(PomodoroSnapshot s) {
    QMetaObject::invokeMethod(
        worker_, [this, s] { worker_->savePomodoro(s); }, Qt::QueuedConnection);
}
} // namespace qding
