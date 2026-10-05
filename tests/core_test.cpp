#include "domain/pomodoro.h"
#include "domain/schedule.h"
#include "services/media.h"
#include "storage/storage.h"
#include <QImage>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace qding;

class CoreTest final : public QObject {
    Q_OBJECT
private slots:
    void weekdaysAndBoundary() {
        ScheduleRule r;
        r.kind = RuleKind::Weekly;
        r.weekdays = 31;
        r.time = QTime(15, 0);
        r.timezone = QStringLiteral("UTC");
        r.effectiveFrom = QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const auto friday = QDateTime(QDate(2026, 10, 9), QTime(15, 0), QTimeZone::UTC);
        QCOMPARE(ScheduleCalculator::next(r, friday.addSecs(-1)).value(), friday);
        QCOMPARE(ScheduleCalculator::next(r, friday).value(),
                 QDateTime(QDate(2026, 10, 12), QTime(15, 0), QTimeZone::UTC));
        QCOMPARE(ScheduleCalculator::latest(r, friday.addDays(-3), friday.addDays(2)).value(),
                 friday);
    }
    void daylightSaving() {
        if (!QTimeZone::isTimeZoneIdAvailable("America/New_York"))
            QSKIP("Timezone database unavailable");
        ScheduleRule r;
        r.timezone = QStringLiteral("America/New_York");
        r.time = QTime(2, 30);
        r.effectiveFrom = QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const auto before = QDateTime(QDate(2026, 3, 8), QTime(0, 0), QTimeZone::UTC);
        const auto next = ScheduleCalculator::next(r, before).value();
        QCOMPARE(next.toTimeZone(QTimeZone("America/New_York")).date(), QDate(2026, 3, 9));
        r.time = QTime(1, 30);
        const auto fold =
            ScheduleCalculator::next(r, QDateTime(QDate(2026, 11, 1), QTime(0, 0), QTimeZone::UTC))
                .value();
        QCOMPARE(fold, QDateTime(QDate(2026, 11, 1), QTime(5, 30), QTimeZone::UTC));
    }
    void pomodoroTransitions() {
        PomodoroEngine engine;
        engine.configure(1, 1);
        engine.startOrResume();
        engine.advance(60000);
        QCOMPARE(engine.snapshot().phase, PomodoroPhase::Break);
        QCOMPARE(engine.snapshot().rounds, 1);
        engine.pause();
        const auto remaining = engine.snapshot().remainingMs;
        engine.advance(60000);
        QCOMPARE(engine.snapshot().remainingMs, remaining);
        engine.startOrResume();
        engine.advance(60000);
        QCOMPARE(engine.snapshot().phase, PomodoroPhase::Work);
        auto state = engine.snapshot();
        engine.restore(state);
        QCOMPARE(engine.snapshot().status, PomodoroStatus::Paused);
    }
    void changingConfigurationDoesNotAlterCurrentPhase() {
        PomodoroEngine engine;
        engine.configure(1, 1);
        engine.startOrResume();
        engine.advance(10000);
        engine.configure(10, 2);
        QCOMPARE(engine.snapshot().phaseDurationMs, 60000);
        QVERIFY(engine.snapshot().remainingMs <= 50000);
        engine.advance(50000);
        QCOMPARE(engine.snapshot().phase, PomodoroPhase::Break);
        QCOMPARE(engine.snapshot().phaseDurationMs, 120000);
    }
    void persistedSnapshotKeepsOriginalPhaseDuration() {
        QTemporaryDir temp;
        const auto file = temp.filePath(QStringLiteral("snapshot.sqlite"));
        PomodoroSnapshot state;
        state.status = PomodoroStatus::Running;
        state.workMinutes = 40; // 下一轮配置，而当前阶段仍是 25 分钟。
        state.remainingMs = 900000;
        state.phaseDurationMs = 1500000;
        {
            StorageService storage(file);
            QSignalSpy ready(&storage, &StorageService::ready);
            storage.start();
            QVERIFY(ready.wait());
            storage.savePomodoro(state);
            storage.shutdown(); // 收尾必须完成队列中的快照写入。
        }
        StorageService reopened(file);
        QSignalSpy ready(&reopened, &StorageService::ready);
        reopened.start();
        QVERIFY(ready.wait());
        const auto restored = ready.first()[1].value<PomodoroSnapshot>();
        QCOMPARE(restored.workMinutes, 40);
        QCOMPARE(restored.remainingMs, 900000);
        QCOMPARE(restored.phaseDurationMs, 1500000);
    }
    void newerDatabaseIsNeverModifiedByShutdownSnapshot() {
        QTemporaryDir temp;
        const auto file = temp.filePath(QStringLiteral("future.sqlite"));
        const auto connection = QStringLiteral("future-fixture");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version=99")));
            QVERIFY(
                q.exec(QStringLiteral("CREATE TABLE pomodoro(id INTEGER PRIMARY KEY,phase "
                                      "INTEGER,status INTEGER,remaining_ms INTEGER,work_minutes "
                                      "INTEGER,break_minutes INTEGER,rounds INTEGER)")));
            QVERIFY(
                q.exec(QStringLiteral("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT)")));
        }
        QSqlDatabase::removeDatabase(connection);
        {
            StorageService storage(file);
            QSignalSpy errors(&storage, &StorageService::error);
            storage.start();
            QVERIFY(errors.wait());
            storage.savePomodoro(PomodoroSnapshot{});
            storage.shutdown();
        }
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM pomodoro")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void storageDedupSnoozeAndRestart() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto file = temp.filePath(QStringLiteral("qding.sqlite"));
        QString id;
        {
            StorageService storage(file);
            QSignalSpy ready(&storage, &StorageService::ready);
            QSignalSpy errors(&storage, &StorageService::error);
            storage.start();
            QVERIFY(ready.wait());
            Reminder r;
            r.title = QStringLiteral("测试喝水");
            ScheduleRule rule;
            rule.kind = RuleKind::Once;
            rule.onceAt = QDateTime::currentDateTimeUtc().addSecs(60);
            r.rules = {rule};
            QSignalSpy saved(&storage, &StorageService::reminderSaved);
            storage.saveReminder(r);
            QVERIFY(saved.wait());
            QSignalSpy scans(&storage, &StorageService::scanFinished);
            storage.scan(rule.onceAt.addSecs(-5), false, 600);
            QVERIFY(scans.wait());
            storage.scan(rule.onceAt, false, 600);
            QVERIFY(scans.wait());
            auto rows = scans.last()[0].value<OccurrenceList>();
            QCOMPARE(rows.size(), 1);
            id = rows.first().id;
            storage.scan(rule.onceAt.addSecs(1), false, 600);
            QVERIFY(scans.wait());
            QVERIFY(scans.last()[0].value<OccurrenceList>().isEmpty());
            QSignalSpy changed(&storage, &StorageService::occurrenceChanged);
            storage.handleOccurrence(id, QStringLiteral("snoozed"), rule.onceAt.addSecs(300));
            QVERIFY(changed.wait());
            storage.scan(rule.onceAt.addSecs(300), false, 600);
            QVERIFY(scans.wait());
            rows = scans.last()[0].value<OccurrenceList>();
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first().id, id);
            QVERIFY(errors.isEmpty());
            storage.shutdown();
        }
        {
            StorageService storage(file);
            QSignalSpy ready(&storage, &StorageService::ready);
            storage.start();
            QVERIFY(ready.wait());
            QSignalSpy history(&storage, &StorageService::historyLoaded);
            storage.loadHistory();
            QVERIFY(history.wait());
            const auto rows = history.last()[0].value<OccurrenceList>();
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first().status, QStringLiteral("interrupted"));
        }
    }
    void quietAndGrace() {
        QTemporaryDir temp;
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        QSignalSpy ready(&storage, &StorageService::ready);
        storage.start();
        QVERIFY(ready.wait());
        Reminder r;
        r.title = QStringLiteral("休息");
        ScheduleRule rule;
        rule.kind = RuleKind::Once;
        rule.onceAt = QDateTime::currentDateTimeUtc().addSecs(60);
        r.rules = {rule};
        QSignalSpy saved(&storage, &StorageService::reminderSaved);
        storage.saveReminder(r);
        QVERIFY(saved.wait());
        QSignalSpy scans(&storage, &StorageService::scanFinished);
        storage.scan(rule.onceAt.addSecs(-1), true, 600);
        QVERIFY(scans.wait());
        storage.scan(rule.onceAt, true, 600);
        QVERIFY(scans.wait());
        QVERIFY(scans.last()[0].value<OccurrenceList>().isEmpty());
        storage.scan(rule.onceAt.addSecs(601), false, 600);
        QVERIFY(scans.wait());
        QVERIFY(scans.last()[0].value<OccurrenceList>().isEmpty());
        QSignalSpy history(&storage, &StorageService::historyLoaded);
        storage.loadHistory();
        QVERIFY(history.wait());
        QCOMPARE(history.last()[0].value<OccurrenceList>().first().status,
                 QStringLiteral("missed"));
    }
    void mediaImportsAndTraversal() {
        QTemporaryDir root, source;
        MediaService media(root.path());
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::red);
        const auto file = source.filePath(QStringLiteral("image.png"));
        QVERIFY(image.save(file));
        QString error;
        const auto relative = media.importFile(file, true, &error);
        QVERIFY2(!relative.isEmpty(), qPrintable(error));
        QVERIFY(!media.resolve(relative).isEmpty());
        QVERIFY(media.resolve(QStringLiteral("../outside.png")).isEmpty());
        QVERIFY(media.resolve(file).isEmpty());
        QVERIFY(!media.defaultSound().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
