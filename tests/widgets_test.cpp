#include "domain/pomodoro.h"
#include "platform/integration.h"
#include "services/media.h"
#include "services/scheduler.h"
#include "services/theme.h"
#include "storage/storage.h"
#include "ui/fireworks.h"
#include "ui/mainwindow.h"
#include "ui/popup.h"
#include "ui/remindereditor.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMovie>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QThread>
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
    void popupSnoozeWaitsForAcknowledgement_data() {
        QTest::addColumn<bool>("celebration");
        QTest::newRow("classic") << false;
        QTest::newRow("celebration") << true;
    }
    void popupSnoozeWaitsForAcknowledgement() {
        QFETCH(bool, celebration);
        QTemporaryDir temp;
        MediaService media(temp.path());
        Occurrence occurrence;
        occurrence.id = QStringLiteral("test-occurrence");
        occurrence.title = QStringLiteral("喝水");
        occurrence.body = QStringLiteral("自定义提醒内容");
        occurrence.scheduledAt = QDateTime::currentDateTimeUtc();
        ReminderPopup popup(
            occurrence, &media,
            {celebration ? PopupStyle::Celebration : PopupStyle::Classic, true, false});
        popup.setAttribute(Qt::WA_DeleteOnClose, false);
        popup.show();
        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const auto available = screen->availableGeometry();
        QVERIFY((popup.frameGeometry().center() - available.center()).manhattanLength() <= 2);
        QCOMPARE(popup.windowFlags().testFlag(Qt::FramelessWindowHint), celebration);
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
    void scheduledReminderCompletesAndPersistsHistory_data() {
        QTest::addColumn<bool>("celebration");
        QTest::newRow("classic") << false;
        QTest::newRow("celebration") << true;
    }
    void scheduledReminderCompletesAndPersistsHistory() {
        QFETCH(bool, celebration);
        QTemporaryDir temp;
        MediaService media(temp.path());
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        ReminderScheduler scheduler(&storage);
        NotificationCoordinator notifications(&media);
        notifications.setMuted(true);
        notifications.setPresentation(
            {celebration ? PopupStyle::Celebration : PopupStyle::Classic, true, false});
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
    void gifResourceIsDecodedByPopup_data() {
        QTest::addColumn<bool>("celebration");
        QTest::newRow("classic") << false;
        QTest::newRow("celebration") << true;
    }
    void gifResourceIsDecodedByPopup() {
        QFETCH(bool, celebration);
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
        ReminderPopup popup(
            occurrence, &media,
            {celebration ? PopupStyle::Celebration : PopupStyle::Classic, false, false});
        popup.setAttribute(Qt::WA_DeleteOnClose, false);
        auto *movie = popup.findChild<QMovie *>();
        QVERIFY(movie);
        QVERIFY(movie->isValid());
        QCOMPARE(movie->currentImage().size(), celebration ? QSize(180, 180) : QSize(240, 240));
    }
    void popupSettingsPersistAndSilentPreviewWorksDuringQuiet() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        MediaService media(temp.path());
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        ThemeManager theme;
        theme.apply(QStringLiteral("light"));
        PomodoroEngine pomodoro;
        ReminderScheduler scheduler(&storage);
        PlatformIntegration platform;
        NotificationCoordinator notifications(&media);
        MainWindow window(&storage, &media, &theme, &pomodoro, &scheduler, &platform,
                          &notifications, &settings);
        window.show();
        QSignalSpy ready(&storage, &StorageService::ready);
        storage.start();
        QVERIFY(ready.wait());
        window.selectPage(4);
        window.findChild<QComboBox *>(QStringLiteral("popupStyle"))->setCurrentIndex(1);
        window.findChild<QCheckBox *>(QStringLiteral("centerPopup"))->setChecked(true);
        window.findChild<QCheckBox *>(QStringLiteral("popupAnimations"))->setChecked(false);
        notifications.setQuiet(true);
        QSignalSpy historyWrites(&notifications, &NotificationCoordinator::handled);
        window.findChild<QPushButton *>(QStringLiteral("previewPopup"))->click();
        auto *preview = window.findChild<ReminderPopup *>();
        QVERIFY(preview && preview->isVisible());
        QVERIFY(preview->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!preview->findChild<FireworksWidget *>()->isAnimating());
        QPointer<ReminderPopup> guard(preview);
        QTest::mouseClick(preview->findChild<QPushButton *>(QStringLiteral("completeButton")),
                          Qt::LeftButton);
        QTRY_VERIFY(guard.isNull());
        QCOMPARE(historyWrites.size(), 0); // 主动样式预览不进入实际提醒处理链。
        settings.sync();
        QSettings restored(settings.fileName(), QSettings::IniFormat);
        QCOMPARE(restored.value(QStringLiteral("popup/style")).toString(),
                 QStringLiteral("celebration"));
        QVERIFY(restored.value(QStringLiteral("popup/center")).toBool());
        QVERIFY(!restored.value(QStringLiteral("popup/animations")).toBool());
    }
    void fireworksStopWhenPopupIsHidden() {
        QTemporaryDir temp;
        MediaService media(temp.path());
        Occurrence occurrence;
        occurrence.title = QStringLiteral("动画提醒");
        ReminderPopup popup(occurrence, &media, {PopupStyle::Celebration, true, true});
        popup.setAttribute(Qt::WA_DeleteOnClose, false);
        popup.show();
        auto *fireworks = popup.findChild<FireworksWidget *>();
        QVERIFY(fireworks->isAnimating());
        popup.hide();
        QVERIFY(!fireworks->isAnimating());
        fireworks->setAnimationEnabled(false);
        popup.show();
        QVERIFY(!fireworks->isAnimating());
        popup.dismissWithoutAction();
    }
    void schedulerRecoversWhenGuiReceivesAnOldScanResult() {
        QTemporaryDir temp;
        StorageService storage(temp.filePath(QStringLiteral("db.sqlite")));
        ReminderScheduler scheduler(&storage);
        connect(&storage, &StorageService::remindersLoaded, &scheduler,
                &ReminderScheduler::setReminders);
        QSignalSpy initialized(&storage, &StorageService::ready);
        storage.start();
        QVERIFY(initialized.wait());
        Reminder reminder;
        reminder.title = QStringLiteral("边界时刻");
        ScheduleRule rule;
        rule.kind = RuleKind::Once;
        rule.onceAt = QDateTime::currentDateTimeUtc().addSecs(2);
        reminder.rules = {rule};
        QSignalSpy saved(&storage, &StorageService::reminderSaved);
        storage.saveReminder(reminder);
        QVERIFY(saved.wait());
        QSignalSpy due(&scheduler, &ReminderScheduler::due);
        // Worker 扫描的是到期前的时刻；模拟 GUI 忙于工作，到期后才接收结果。
        storage.scan(QDateTime::currentDateTimeUtc(), false, 600);
        QThread::msleep(2200);
        scheduler.start();
        bool delivered = false;
        QElapsedTimer timeout;
        timeout.start();
        while (!delivered && timeout.elapsed() < 1500) {
            due.wait(1500 - timeout.elapsed());
            for (const auto &result : due)
                delivered |= !result.first().value<OccurrenceList>().isEmpty();
        }
        scheduler.stop();
        QVERIFY2(delivered, "A due reminder must not wait for the 30-second fallback poll");
    }
};
QTEST_MAIN(WidgetsTest)
#include "widgets_test.moc"
