#include "domain/pomodoro.h"
#include "logging/logging.h"
#include "platform/integration.h"
#include "services/media.h"
#include "services/scheduler.h"
#include "services/theme.h"
#include "storage/storage.h"
#include "ui/mainwindow.h"
#include "ui/popup.h"
#include "ui/remindereditor.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QGroupBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <spdlog/spdlog.h>

namespace {
QIcon applicationIcon() {
    QIcon icon;
    // 标准尺寸有预渲染 PNG，避免细小图标随 SVG/iconengines 插件加载时机变化。
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/branding/qding-%1.png").arg(size), QSize(size, size));
    // SVG 条目仍需要发布包带上 SVG 相关插件（见 docs/qt-plugins.zh-CN.md）。
    icon.addFile(QStringLiteral(":/branding/qding.svg"));
    return icon;
}
} // namespace

int main(int argc, char **argv) {
    // 此处加载 QPA 平台插件（windows/cocoa，或环境变量 QT_QPA_PLATFORM=offscreen）。
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("lpxxn"));
    QCoreApplication::setApplicationName(QStringLiteral("qDing"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setWindowIcon(applicationIcon());
    QApplication::setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("qDing — 本地提醒与番茄钟（Qt Widgets / C++20）"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("background"), QStringLiteral("隐藏主窗口，从托盘启动")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("指定独立的数据目录"),
                      QStringLiteral("directory")});
    parser.addOption(
        {QStringLiteral("smoke-test"), QStringLiteral("临时目录、无声音运行界面检查，然后退出")});
    parser.addOption({QStringLiteral("screenshot-dir"),
                      QStringLiteral("smoke-test 时保存各页面截图"), QStringLiteral("directory")});
    parser.process(app);
    const bool smoke = parser.isSet(QStringLiteral("smoke-test"));
    QTemporaryDir temporary;
    if (smoke && !temporary.isValid())
        return 2;
    const auto dataRoot =
        smoke ? temporary.path()
        : parser.isSet(QStringLiteral("data-dir"))
            ? QDir(parser.value(QStringLiteral("data-dir"))).absolutePath()
            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataRoot)) {
        QMessageBox::critical(nullptr, QStringLiteral("无法启动"),
                              QStringLiteral("无法创建应用数据目录。"));
        return 1;
    }
    const auto serverName =
        QStringLiteral("qding-") +
        QString::fromLatin1(QCryptographicHash::hash(dataRoot.toUtf8(), QCryptographicHash::Sha256)
                                .toHex()
                                .left(24));
    QLockFile lock(dataRoot + QStringLiteral("/instance.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(1000)) {
            socket.write("show\n");
            socket.flush();
            socket.waitForBytesWritten(1000);
        }
        return 0;
    }
    try {
        qding::initializeLogging(dataRoot + QStringLiteral("/logs"));
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, QStringLiteral("日志初始化失败"),
                              QString::fromUtf8(e.what()));
        return 1;
    }
    spdlog::info("qDing started version={} Qt={} platform={}", "0.1.0", qVersion(),
                 QSysInfo::productType().toStdString());

    qding::MediaService media(dataRoot);
    QSettings settings(dataRoot + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    qding::ThemeManager theme;
    theme.apply(
        settings.value(QStringLiteral("theme/mode"), QStringLiteral("system")).toString(),
        QColor(
            settings.value(QStringLiteral("theme/accent"), QStringLiteral("#e36d57")).toString()));
    qding::StorageService storage(dataRoot + QStringLiteral("/qding.sqlite"));
    qding::PomodoroEngine pomodoro;
    qding::ReminderScheduler scheduler(&storage);
    qding::PlatformIntegration platform;
    qding::NotificationCoordinator notifications(&media);
    qding::MainWindow window(&storage, &media, &theme, &pomodoro, &scheduler, &platform,
                             &notifications, &settings);
    if (smoke)
        notifications.setMuted(true);

    QLocalServer server;
    QLocalServer::removeServer(serverName);
    if (!server.listen(serverName))
        spdlog::warn("local activation server failed");
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            socket->setReadBufferSize(64);
            auto activate = [socket, &window] {
                if (!socket->canReadLine())
                    return;
                if (socket->readLine().trimmed() == "show")
                    window.showAndRaise();
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &window, activate);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            activate(); // 接受连接时数据可能已经到达，不能只等待下一次 readyRead。
            QTimer::singleShot(2000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
        }
    });

    QObject::connect(
        &storage, &qding::StorageService::ready, &window,
        [&](qding::ReminderList list, qding::PomodoroSnapshot state) {
            pomodoro.restore(state);
            scheduler.setReminders(std::move(list));
            scheduler.start();
            if (smoke) {
                qding::Reminder sample;
                sample.title = QStringLiteral("喝一杯水");
                sample.body = QStringLiteral("起身活动一下，给自己补充水分。");
                qding::ScheduleRule daily;
                daily.time = QTime(10, 0);
                sample.rules = {daily};
                sample.volume = 0;
                storage.saveReminder(sample);
                qding::Reminder weekday;
                weekday.title = QStringLiteral("午后休息");
                weekday.body = QStringLiteral("让眼睛休息，看看远处。");
                qding::ScheduleRule rule;
                rule.kind = qding::RuleKind::Weekly;
                rule.weekdays = 31;
                rule.time = QTime(15, 0);
                weekday.rules = {rule};
                weekday.volume = 0;
                storage.saveReminder(weekday);
                QTimer::singleShot(600, &window, [&, sample] {
                    const auto screenshots = parser.value(QStringLiteral("screenshot-dir"));
                    if (!screenshots.isEmpty()) {
                        window.capturePages(screenshots);
                        theme.apply(QStringLiteral("dark"));
                        window.capturePages(screenshots + QStringLiteral("/dark"));
                        for (const auto &mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
                            theme.apply(mode);
                            const auto directory = mode == QLatin1String("light")
                                                       ? screenshots
                                                       : screenshots + QStringLiteral("/dark");
                            qding::ReminderEditor editor(sample, &storage, &media, &window);
                            editor.show();
                            editor.grab().save(directory + QStringLiteral("/editor.png"));
                            auto *combo = editor.findChild<QComboBox *>(QStringLiteral("kind"));
                            combo->parentWidget()->grab().save(
                                directory + QStringLiteral("/rule-controls.png"));
                            combo->showPopup();
                            combo->view()->window()->grab().save(
                                directory + QStringLiteral("/rule-options.png"));
                            combo->hidePopup();
                            combo->setCurrentIndex(3);
                            editor.grab().save(directory + QStringLiteral("/editor-once.png"));
                        }
                        theme.apply(QStringLiteral("light"));
                        qding::Occurrence occurrence;
                        occurrence.id = QStringLiteral("preview:test");
                        occurrence.title = sample.title;
                        occurrence.body = sample.body;
                        occurrence.scheduledAt = QDateTime::currentDateTimeUtc();
                        qding::ReminderPopup popup(occurrence, &media);
                        popup.setAttribute(Qt::WA_DeleteOnClose, false);
                        popup.show();
                        popup.grab().save(screenshots + QStringLiteral("/popup.png"));
                        window.selectPage(4);
                        window.findChild<QComboBox *>(QStringLiteral("popupStyle"))
                            ->setCurrentIndex(1);
                        window.findChild<QCheckBox *>(QStringLiteral("centerPopup"))
                            ->setChecked(true);
                        auto *scroll =
                            window.findChild<QScrollArea *>(QStringLiteral("scrollArea"));
                        scroll->ensureWidgetVisible(
                            window.findChild<QGroupBox *>(QStringLiteral("popupGroup")));
                        window.grab().save(screenshots + QStringLiteral("/settings-popup.png"));
                        auto *celebration = new qding::ReminderPopup(
                            occurrence, &media, {qding::PopupStyle::Celebration, true, true},
                            &window);
                        celebration->show();
                        QTimer::singleShot(950, &window, [&, screenshots, celebration] {
                            celebration->grab().save(screenshots +
                                                     QStringLiteral("/celebration.png"));
                            theme.apply(QStringLiteral("dark"));
                            celebration->grab().save(screenshots +
                                                     QStringLiteral("/dark/celebration.png"));
                            theme.apply(QStringLiteral("light"), QColor("#2b9d86"));
                            celebration->grab().save(screenshots +
                                                     QStringLiteral("/celebration-teal.png"));
                            celebration->dismissWithoutAction();
                            window.requestExit();
                        });
                        return; // 让事件循环推进烟花动画，再截图并结束 smoke。
                    }
                    window.requestExit();
                });
            }
        });
    QObject::connect(&storage, &qding::StorageService::remindersLoaded, &scheduler,
                     &qding::ReminderScheduler::setReminders);
    QObject::connect(&storage, &qding::StorageService::reminderSaved, &notifications,
                     [&](const qding::Reminder &r) { notifications.cancelReminder(r.id); });
    QObject::connect(&storage, &qding::StorageService::reminderRemoved, &notifications,
                     &qding::NotificationCoordinator::cancelReminder);
    QObject::connect(&scheduler, &qding::ReminderScheduler::due, &notifications,
                     &qding::NotificationCoordinator::enqueue);
    QObject::connect(
        &notifications, &qding::NotificationCoordinator::displayed, &storage,
        [&](const QString &id) { storage.handleOccurrence(id, QStringLiteral("shown")); });
    QObject::connect(&notifications, &qding::NotificationCoordinator::handled, &storage,
                     [&](QString id, QString status, int minutes) {
                         storage.handleOccurrence(
                             id, status,
                             status == QLatin1String("snoozed")
                                 ? QDateTime::currentDateTimeUtc().addSecs(minutes * 60)
                                 : QDateTime{});
                     });
    QObject::connect(&storage, &qding::StorageService::occurrenceChanged, &notifications,
                     &qding::NotificationCoordinator::acknowledge);
    QObject::connect(&pomodoro, &qding::PomodoroEngine::checkpoint, &storage,
                     &qding::StorageService::savePomodoro);
    QObject::connect(&pomodoro, &qding::PomodoroEngine::phaseFinished, &window,
                     [&](qding::PomodoroPhase phase) {
                         qding::Reminder message;
                         message.title = phase == qding::PomodoroPhase::Work
                                             ? QStringLiteral("专注完成，休息一下")
                                             : QStringLiteral("休息结束，开始下一轮");
                         message.body = phase == qding::PomodoroPhase::Work
                                            ? QStringLiteral("起身伸展，给眼睛和身体一点休息时间。")
                                            : QStringLiteral("准备好了，就投入下一段专注时间吧。");
                         notifications.preview(message, smoke);
                         spdlog::info("pomodoro phase completed phase={}", static_cast<int>(phase));
                     });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&] {
        scheduler.stop();
        pomodoro.pause();
        storage.savePomodoro(pomodoro.snapshot());
        settings.setValue(QStringLiteral("window/geometry"), window.saveGeometry());
        settings.sync();
        notifications.stopSound();
    });
    int result = 0;
    if (smoke) {
        QObject::connect(&storage, &qding::StorageService::error, &app, [&](const QString &) {
            result = 1;
            app.quit();
        });
        QTimer::singleShot(10000, &app, [&] {
            result = 2;
            app.quit();
        });
    }
    if ((!parser.isSet(QStringLiteral("background")) &&
         !settings.value(QStringLiteral("behavior/startInTray"), false).toBool()) ||
        !window.trayAvailable() || smoke)
        window.show();
    storage.start();
    const int exitCode = app.exec();
    storage.shutdown();
    spdlog::info("qDing exited");
    spdlog::default_logger()->flush();
    // QObject 清理仍可能输出 Qt 日志，让默认 logger 活到函数局部对象销毁之后。
    return result == 0 ? exitCode : result;
}
