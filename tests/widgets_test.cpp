#include "services/media.h"
#include "services/scheduler.h"
#include "services/theme.h"
#include "storage/storage.h"
#include "ui/popup.h"
#include "ui/remindereditor.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMovie>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimeEdit>

using namespace qding;

class WidgetsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion"))); }
    void designerEditorSavesWeekdayAndSecondTime() {
        QTemporaryDir temp;
        MediaService media(temp.path());
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        QSignalSpy initialized(&storage, &StorageService::ready);
        storage.start();
        QVERIFY(initialized.wait());
        ThemeManager theme;
        theme.apply(QStringLiteral("light"));
        ReminderEditor editor(Reminder{}, &storage, &media);
        editor.show();
        editor.findChild<QLineEdit *>(QStringLiteral("titleEdit"))
            ->setText(QStringLiteral("喝水和休息"));
        editor.findChild<QTextEdit *>(QStringLiteral("bodyEdit"))
            ->setPlainText(QStringLiteral("自定义内容"));
        editor.findChildren<QComboBox *>(QStringLiteral("kind")).first()->setCurrentIndex(1);
        editor.findChildren<QTimeEdit *>(QStringLiteral("timeEdit")).first()->setTime(QTime(15, 0));
        QTest::mouseClick(editor.findChild<QPushButton *>(QStringLiteral("addRule")),
                          Qt::LeftButton);
        QCOMPARE(editor.findChildren<QComboBox *>(QStringLiteral("kind")).size(), 2);
        QSignalSpy saved(&storage, &StorageService::reminderSaved);
        auto *box = editor.findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
        QTest::mouseClick(box->button(QDialogButtonBox::Save), Qt::LeftButton);
        QTRY_COMPARE(saved.size(), 1);
        const auto reminder = saved.first()[0].value<Reminder>();
        QCOMPARE(reminder.title, QStringLiteral("喝水和休息"));
        QCOMPARE(reminder.body, QStringLiteral("自定义内容"));
        QCOMPARE(reminder.rules.size(), 2);
        QCOMPARE(reminder.rules.first().weekdays, 31);
        QCOMPARE(reminder.rules.first().time, QTime(15, 0));
        QCOMPARE(reminder.rules.last().kind, RuleKind::Daily);
        QCOMPARE(editor.result(), int(QDialog::Accepted));
    }
    void popupSnoozeWaitsForAcknowledgement() {
        QTemporaryDir temp;
        MediaService media(temp.path());
        Occurrence occurrence;
        occurrence.id = QStringLiteral("test-occurrence");
        occurrence.title = QStringLiteral("喝水");
        occurrence.body = QStringLiteral("自定义提醒内容");
        occurrence.scheduledAt = QDateTime::currentDateTimeUtc();
        ReminderPopup popup(occurrence, &media);
        popup.setAttribute(Qt::WA_DeleteOnClose, false);
        popup.show();
        QSignalSpy actions(&popup, &ReminderPopup::action);
        popup.findChild<QComboBox *>(QStringLiteral("snoozeDuration"))->setCurrentIndex(1);
        QTest::mouseClick(popup.findChild<QPushButton *>(QStringLiteral("snoozeButton")),
                          Qt::LeftButton);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first()[1].toString(), QStringLiteral("snoozed"));
        QCOMPARE(actions.first()[2].toInt(), 10);
        QVERIFY(popup.isVisible()); // 数据库确认前窗口保留，失败时允许重试。
        QVERIFY(!popup.findChild<QPushButton *>(QStringLiteral("completeButton"))->isEnabled());
        popup.retry();
        QVERIFY(popup.findChild<QPushButton *>(QStringLiteral("completeButton"))->isEnabled());
        popup.dismissWithoutAction();
        QVERIFY(!popup.isVisible());
    }
    void scheduledReminderCompletesAndPersistsHistory() {
        QTemporaryDir temp;
        MediaService media(temp.path());
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        ReminderScheduler scheduler(&storage);
        NotificationCoordinator notifications(&media);
        notifications.setMuted(true);
        connect(&storage, &StorageService::remindersLoaded, &scheduler,
                &ReminderScheduler::setReminders);
        connect(&scheduler, &ReminderScheduler::due, &notifications,
                &NotificationCoordinator::enqueue);
        connect(&notifications, &NotificationCoordinator::displayed, &storage,
                [&](const QString &id) { storage.handleOccurrence(id, QStringLiteral("shown")); });
        connect(&notifications, &NotificationCoordinator::handled, &storage,
                [&](const QString &id, const QString &status, int) {
                    storage.handleOccurrence(id, status);
                });
        connect(&storage, &StorageService::occurrenceChanged, &notifications,
                &NotificationCoordinator::acknowledge);
        QSignalSpy initialized(&storage, &StorageService::ready);
        storage.start();
        QVERIFY(initialized.wait());
        Reminder reminder;
        reminder.title = QStringLiteral("完整流程提醒");
        ScheduleRule rule;
        rule.kind = RuleKind::Once;
        rule.onceAt = QDateTime::currentDateTimeUtc().addSecs(2);
        reminder.rules = {rule};
        QSignalSpy saved(&storage, &StorageService::reminderSaved);
        storage.saveReminder(reminder);
        QVERIFY(saved.wait());
        QSignalSpy shown(&notifications, &NotificationCoordinator::displayed);
        scheduler.start();
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 1, 5000);
        ReminderPopup *popup = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *candidate = qobject_cast<ReminderPopup *>(widget);
                candidate && candidate->occurrence().reminderId == reminder.id)
                popup = candidate;
        QVERIFY(popup);
        QPointer<ReminderPopup> guard(popup);
        QTest::mouseClick(popup->findChild<QPushButton *>(QStringLiteral("completeButton")),
                          Qt::LeftButton);
        QTRY_VERIFY(guard.isNull()); // 确认存储线程回应后弹窗才销毁。
        QSignalSpy history(&storage, &StorageService::historyLoaded);
        storage.loadHistory();
        QVERIFY(history.wait());
        const auto rows = history.first()[0].value<OccurrenceList>();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().status, QStringLiteral("completed"));
        QCOMPARE(rows.first().title, reminder.title);
        scheduler.stop();
    }
    void gifResourceIsDecodedByPopup() {
        QTemporaryDir temp;
        MediaService media(temp.path());
        QFile file(temp.filePath(QStringLiteral("tiny.gif")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
        file.close();
        QString error;
        const auto asset = media.importFile(file.fileName(), true, &error);
        QVERIFY2(!asset.isEmpty(), qPrintable(error));
        Occurrence occurrence;
        occurrence.id = QStringLiteral("test-gif");
        occurrence.title = QStringLiteral("GIF 提醒");
        occurrence.visual = asset;
        ReminderPopup popup(occurrence, &media);
        popup.setAttribute(Qt::WA_DeleteOnClose, false);
        auto *movie = popup.findChild<QMovie *>();
        QVERIFY(movie);
        QVERIFY(movie->isValid());
        QCOMPARE(movie->currentImage().size(), QSize(240, 240));
    }
};
QTEST_MAIN(WidgetsTest)
#include "widgets_test.moc"
