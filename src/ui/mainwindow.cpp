#include "ui/mainwindow.h"
#include "domain/pomodoro.h"
#include "platform/integration.h"
#include "services/media.h"
#include "services/scheduler.h"
#include "services/theme.h"
#include "storage/storage.h"
#include "ui/countdownring.h"
#include "ui/popup.h"
#include "ui/remindereditor.h"
#include "ui/remindermodel.h"
#include "ui_historypage.h"
#include "ui_mainwindow.h"
#include "ui_pomodoropage.h"
#include "ui_reminderspage.h"
#include "ui_settingspage.h"
#include "ui_todaypage.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QVBoxLayout>
#include <spdlog/spdlog.h>

namespace qding {
namespace {
QString statusLabel(const QString &status) {
    const QHash<QString, QString> labels{
        {"claimed", QStringLiteral("等待展示")}, {"shown", QStringLiteral("已展示")},
        {"completed", QStringLiteral("已完成")}, {"dismissed", QStringLiteral("已忽略")},
        {"snoozed", QStringLiteral("稍后提醒")}, {"deferred", QStringLiteral("勿扰延后")},
        {"missed", QStringLiteral("已错过")},    {"interrupted", QStringLiteral("上次运行中断")},
        {"cancelled", QStringLiteral("已取消")}, {"pending", QStringLiteral("待提醒")}};
    return labels.value(status, status);
}
} // namespace

MainWindow::MainWindow(StorageService *storage, MediaService *media, ThemeManager *theme,
                       PomodoroEngine *pomodoro, ReminderScheduler *scheduler,
                       PlatformIntegration *platform, NotificationCoordinator *notifications,
                       QSettings *settings)
    : ui_(std::make_unique<Ui::MainWindow>()), storage_(storage), media_(media), theme_(theme),
      pomodoro_(pomodoro), scheduler_(scheduler), platform_(platform),
      notifications_(notifications), settings_(settings) {
    ui_->setupUi(this);
    setMinimumSize(960, 680);
    ui_->brand->setProperty("role", "brand");
    ui_->tagline->setProperty("role", "muted");
    ui_->heading->setProperty("role", "heading");
    ui_->subtitle->setProperty("role", "muted");
    model_ = new ReminderListModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    ui_->pages->addWidget(createToday());
    ui_->pages->addWidget(createReminders());
    ui_->pages->addWidget(createPomodoro());
    ui_->pages->addWidget(createHistory());
    ui_->pages->addWidget(createSettings());
    ui_->navigation->addItems({QStringLiteral("今日"), QStringLiteral("提醒事项"),
                               QStringLiteral("番茄钟"), QStringLiteral("提醒历史"),
                               QStringLiteral("设置")});
    connect(ui_->navigation, &QListWidget::currentRowChanged, this, [this](int row) {
        ui_->pages->setCurrentIndex(row);
        ui_->heading->setText(ui_->navigation->item(row)->text());
        const QStringList subtitles{QStringLiteral("把时间留给重要的事。"),
                                    QStringLiteral("让每一个重要时刻，都有一个温柔的提醒。"),
                                    QStringLiteral("专注一段时间，再好好休息。"),
                                    QStringLiteral("查看最近 300 次计划发生与处理结果。"),
                                    QStringLiteral("让 qDing 按照你的习惯工作。")};
        ui_->subtitle->setText(subtitles.value(row));
        if (row == 3 && ready_)
            storage_->loadHistory();
    });
    ui_->navigation->setCurrentRow(0);
    restoreGeometry(settings_->value(QStringLiteral("window/geometry")).toByteArray());
    createTray();
    ui_->pages->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在初始化本地数据库…"));
    connect(storage_, &StorageService::ready, this, [this](ReminderList list, PomodoroSnapshot) {
        ready_ = true;
        ui_->pages->setEnabled(true);
        updateReminders(std::move(list));
        statusBar()->showMessage(QStringLiteral("所有提醒均保存在本机"), 5000);
    });
    connect(storage_, &StorageService::remindersLoaded, this, &MainWindow::updateReminders);
    connect(storage_, &StorageService::error, this, [this](const QString &message) {
        statusBar()->showMessage(message);
        notifications_->retry();
        if (!ready_)
            QMessageBox::critical(this, QStringLiteral("初始化失败"), message);
    });
    connect(storage_, &StorageService::historyLoaded, this, [this](OccurrenceList rows) {
        history_->setRowCount(static_cast<int>(rows.size()));
        for (int i = 0; i < rows.size(); ++i) {
            const auto &o = rows[i];
            const QStringList values{
                o.title, o.scheduledAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")),
                statusLabel(o.status),
                o.deliveryAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"))};
            for (int c = 0; c < 4; ++c)
                history_->setItem(i, c, new QTableWidgetItem(values[c]));
        }
    });
    connect(storage_, &StorageService::occurrenceChanged, this, [this](const Occurrence &) {
        if (ui_->pages->currentIndex() == 3)
            storage_->loadHistory();
    });
    connect(scheduler_, &ReminderScheduler::checked, this, [this](QDateTime) { updateToday(); });
    connect(pomodoro_, &PomodoroEngine::changed, this, &MainWindow::updatePomodoro);
    connect(theme_, &ThemeManager::changed, this, [this] { list_->viewport()->update(); });
    connect(platform_, &PlatformIntegration::aboutToSleep, this, [this] {
        sleeping_ = true;
        pomodoro_->pause();
        refreshQuiet();
        spdlog::info("system entering sleep");
    });
    connect(platform_, &PlatformIntegration::resumed, this, [this] {
        sleeping_ = false;
        refreshQuiet();
        scheduler_->wake();
        spdlog::info("system resumed");
    });
    connect(platform_, &PlatformIntegration::lockedChanged, this, [this](bool locked) {
        locked_ = locked;
        refreshQuiet();
    });
    quietTimer_.setInterval(1000);
    connect(&quietTimer_, &QTimer::timeout, this, &MainWindow::refreshQuiet);
    updatePomodoro(pomodoro_->snapshot());
}
MainWindow::~MainWindow() = default;

QWidget *MainWindow::createToday() {
    auto *page = new QWidget;
    Ui::TodayPage form;
    form.setupUi(page);
    nextLabel_ = form.nextValue;
    countLabel_ = form.countValue;
    roundsLabel_ = form.roundsValue;
    connect(form.addReminder, &QPushButton::clicked, this, [this] { editReminder(true); });
    connect(form.startFocus, &QPushButton::clicked, this, [this] {
        selectPage(2);
        pomodoro_->startOrResume();
    });
    return page;
}

QWidget *MainWindow::createReminders() {
    auto *page = new QWidget;
    Ui::RemindersPage form;
    form.setupUi(page);
    connect(form.search, &QLineEdit::textChanged, proxy_,
            &QSortFilterProxyModel::setFilterFixedString);
    emptyLabel_ = form.emptyHint;
    list_ = form.reminderList;
    list_->setModel(proxy_);
    list_->setItemDelegate(new ReminderDelegate(theme_, list_));
    connect(form.addReminder, &QPushButton::clicked, this, [this] { editReminder(true); });
    connect(form.editReminder, &QPushButton::clicked, this, [this] { editReminder(); });
    connect(list_, &QListView::doubleClicked, this, [this] { editReminder(); });
    connect(form.toggleReminder, &QPushButton::clicked, this, [this] {
        if (hasSelection()) {
            auto r = selectedReminder();
            r.enabled = !r.enabled;
            storage_->saveReminder(r);
        }
    });
    connect(form.duplicateReminder, &QPushButton::clicked, this, [this] {
        if (!hasSelection())
            return;
        auto r = selectedReminder();
        r.id = newId();
        r.title += QStringLiteral("（副本）");
        for (auto &rule : r.rules)
            rule.id = newId();
        auto *dialog = new ReminderEditor(r, storage_, media_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &ReminderEditor::previewRequested, notifications_,
                [this](Reminder r) { notifications_->preview(r); });
        dialog->open();
    });
    connect(form.removeReminder, &QPushButton::clicked, this, [this] {
        if (hasSelection() &&
            QMessageBox::question(this, QStringLiteral("删除提醒"),
                                  QStringLiteral("删除这个提醒？触发历史会保留。")) ==
                QMessageBox::Yes)
            storage_->removeReminder(selectedReminder().id);
    });
    connect(form.previewReminder, &QPushButton::clicked, this, [this] {
        if (hasSelection())
            notifications_->preview(selectedReminder());
    });
    const QList<QPushButton *> selectedButtons{form.editReminder, form.toggleReminder,
                                               form.duplicateReminder, form.removeReminder,
                                               form.previewReminder};
    auto updateActions = [this, selectedButtons] {
        for (auto *b : selectedButtons)
            b->setEnabled(hasSelection());
    };
    connect(list_->selectionModel(), &QItemSelectionModel::selectionChanged, this, updateActions);
    connect(model_, &QAbstractItemModel::modelReset, this, updateActions);
    updateActions();
    return page;
}

QWidget *MainWindow::createPomodoro() {
    auto *page = new QWidget;
    Ui::PomodoroPage form;
    form.setupUi(page);
    phaseLabel_ = form.phaseLabel;
    ring_ = form.countdownRing;
    ring_->setTheme(theme_);
    startButton_ = form.startButton;
    workMinutes_ = form.workMinutes;
    breakMinutes_ = form.breakMinutes;
    connect(startButton_, &QPushButton::clicked, this, [this] {
        if (pomodoro_->snapshot().status == PomodoroStatus::Running)
            pomodoro_->pause();
        else
            pomodoro_->startOrResume();
    });
    connect(form.stopButton, &QPushButton::clicked, pomodoro_, &PomodoroEngine::stop);
    auto configure = [this] {
        pomodoro_->configure(workMinutes_->value(), breakMinutes_->value());
    };
    connect(workMinutes_, &QSpinBox::valueChanged, this, configure);
    connect(breakMinutes_, &QSpinBox::valueChanged, this, configure);
    return page;
}

QWidget *MainWindow::createHistory() {
    auto *page = new QWidget;
    Ui::HistoryPage form;
    form.setupUi(page);
    connect(form.refreshHistory, &QPushButton::clicked, storage_, &StorageService::loadHistory);
    history_ = form.historyTable;
    history_->verticalHeader()->hide();
    history_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    history_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    return page;
}

QWidget *MainWindow::createSettings() {
    auto *page = new QWidget;
    Ui::SettingsPage form;
    form.setupUi(page);
    auto *mode = form.themeMode;
    const QStringList modes{QStringLiteral("system"), QStringLiteral("light"),
                            QStringLiteral("dark")};
    for (int i = 0; i < modes.size(); ++i)
        mode->setItemData(i, modes[i]);
    mode->setCurrentIndex(qMax(0, mode->findData(settings_->value(QStringLiteral("theme/mode"),
                                                                  QStringLiteral("system")))));
    auto *accent = form.accent;
    const QStringList colors{QStringLiteral("#e36d57"), QStringLiteral("#2b9d86"),
                             QStringLiteral("#487dd8"), QStringLiteral("#9271d0")};
    for (int i = 0; i < colors.size(); ++i)
        accent->setItemData(i, colors[i]);
    accent->setCurrentIndex(
        qMax(0, accent->findData(
                    settings_->value(QStringLiteral("theme/accent"), QStringLiteral("#e36d57")))));
    auto apply = [this, mode, accent] {
        settings_->setValue(QStringLiteral("theme/mode"), mode->currentData());
        settings_->setValue(QStringLiteral("theme/accent"), accent->currentData());
        theme_->apply(mode->currentData().toString(), QColor(accent->currentData().toString()));
    };
    connect(mode, &QComboBox::currentIndexChanged, this, apply);
    connect(accent, &QComboBox::currentIndexChanged, this, apply);
    form.startInTray->setChecked(
        settings_->value(QStringLiteral("behavior/startInTray"), false).toBool());
    connect(form.startInTray, &QCheckBox::toggled, this, [this](bool value) {
        settings_->setValue(QStringLiteral("behavior/startInTray"), value);
    });
    auto *closeTray = form.closeToTray;
    closeTray->setChecked(settings_->value(QStringLiteral("behavior/closeToTray"), true).toBool());
    connect(closeTray, &QCheckBox::toggled, this, [this](bool value) {
        settings_->setValue(QStringLiteral("behavior/closeToTray"), value);
    });
    auto *login = form.loginStart;
    login->setChecked(platform_->loginEnabled());
    connect(login, &QCheckBox::toggled, this, [this, login](bool value) {
        QString error;
        if (!platform_->setLoginEnabled(value, &error)) {
            QSignalBlocker block(login);
            login->setChecked(platform_->loginEnabled());
            QMessageBox::warning(this, QStringLiteral("登录启动"), error);
        } else if (value && !platform_->loginEnabled()) {
            QSignalBlocker block(login);
            login->setChecked(false);
            QMessageBox::information(
                this, QStringLiteral("登录启动"),
                QStringLiteral("请在系统设置中批准登录启动，然后返回确认状态。"));
        }
    });
    quietBox_ = form.quietMode;
    connect(quietBox_, &QCheckBox::toggled, this,
            [this](bool value) { setQuietMinutes(value ? -1 : 0); });
    connect(form.quietHalfHour, &QPushButton::clicked, this, [this] { setQuietMinutes(30); });
    connect(form.quietHour, &QPushButton::clicked, this, [this] { setQuietMinutes(60); });
    auto *grace = form.graceMinutes;
    grace->setValue(settings_->value(QStringLiteral("reminder/graceMinutes"), 10).toInt());
    scheduler_->setGraceSeconds(grace->value() * 60);
    connect(grace, &QSpinBox::valueChanged, this, [this](int value) {
        settings_->setValue(QStringLiteral("reminder/graceMinutes"), value);
        scheduler_->setGraceSeconds(value * 60);
    });
    auto *mute = form.muteSound;
    mute->setChecked(settings_->value(QStringLiteral("sound/muted"), false).toBool());
    notifications_->setMuted(mute->isChecked());
    connect(mute, &QCheckBox::toggled, this, [this](bool value) {
        settings_->setValue(QStringLiteral("sound/muted"), value);
        notifications_->setMuted(value);
    });
    auto *volume = form.globalVolume;
    volume->setValue(settings_->value(QStringLiteral("sound/volume"), 100).toInt());
    notifications_->setGlobalVolume(volume->value());
    connect(volume, &QSlider::valueChanged, this, [this](int value) {
        settings_->setValue(QStringLiteral("sound/volume"), value);
        notifications_->setGlobalVolume(value);
    });
    form.dataPath->setText(media_->root());
    connect(form.openData, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(media_->root())); });
    connect(form.openLogs, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(media_->root() + QStringLiteral("/logs")));
    });
    return page;
}

void MainWindow::editReminder(bool fresh) {
    if (!fresh && !hasSelection())
        return;
    auto *dialog =
        new ReminderEditor(fresh ? Reminder{} : selectedReminder(), storage_, media_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ReminderEditor::previewRequested, notifications_,
            [this](Reminder r) { notifications_->preview(r); });
    dialog->open();
}
bool MainWindow::hasSelection() const {
    return list_->currentIndex().isValid();
}
Reminder MainWindow::selectedReminder() const {
    return list_->currentIndex().data(ReminderListModel::ReminderRole).value<Reminder>();
}
void MainWindow::updateReminders(ReminderList list) {
    model_->replace(std::move(list));
    emptyLabel_->setVisible(model_->rowCount() == 0);
    if (proxy_->rowCount() > 0)
        list_->setCurrentIndex(proxy_->index(0, 0));
    updateToday();
}
void MainWindow::updateToday() {
    int count = 0;
    for (const auto &r : model_->reminders())
        if (r.enabled)
            ++count;
    countLabel_->setText(QString::number(count));
    const auto next = scheduler_->nextReminder();
    nextLabel_->setText(next.isValid() ? next.toLocalTime().toString(QStringLiteral("HH:mm"))
                                       : QStringLiteral("暂无"));
    nextLabel_->setToolTip(next.isValid()
                               ? next.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                               : QString{});
}
void MainWindow::updatePomodoro(PomodoroSnapshot s) {
    ring_->setSnapshot(s);
    roundsLabel_->setText(QString::number(s.rounds));
    {
        QSignalBlocker a(workMinutes_), b(breakMinutes_);
        workMinutes_->setValue(s.workMinutes);
        breakMinutes_->setValue(s.breakMinutes);
    }
    startButton_->setText(s.status == PomodoroStatus::Running  ? QStringLiteral("暂停")
                          : s.status == PomodoroStatus::Paused ? QStringLiteral("继续")
                                                               : QStringLiteral("开始专注"));
    phaseLabel_->setText(s.status == PomodoroStatus::Idle ? QStringLiteral("准备开始")
                         : s.status == PomodoroStatus::Paused
                             ? QStringLiteral("已暂停 · 可以继续或重置")
                         : s.phase == PomodoroPhase::Work ? QStringLiteral("专注进行中")
                                                          : QStringLiteral("休息进行中"));
    if (tray_) {
        const auto seconds = (s.remainingMs + 999) / 1000;
        const auto text = s.status == PomodoroStatus::Idle
                              ? QStringLiteral("qDing · 提醒与番茄钟")
                              : QStringLiteral("qDing · %1 %2:%3")
                                    .arg(phaseLabel_->text())
                                    .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                                    .arg(seconds % 60, 2, 10, QLatin1Char('0'));
        if (tray_->toolTip() != text)
            tray_->setToolTip(text);
    }
}
void MainWindow::createTray() {
    tray_ = new QSystemTrayIcon(qApp->windowIcon(), this);
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("打开 qDing"), this, &MainWindow::showAndRaise);
    menu->addAction(QStringLiteral("新建提醒"), this, [this] {
        showAndRaise();
        if (ready_)
            editReminder(true);
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("开始 / 暂停番茄钟"), this, [this] {
        if (!ready_)
            return;
        if (pomodoro_->snapshot().status == PomodoroStatus::Running)
            pomodoro_->pause();
        else
            pomodoro_->startOrResume();
    });
    menu->addAction(QStringLiteral("勿扰 30 分钟"), this, [this] { setQuietMinutes(30); });
    menu->addAction(QStringLiteral("恢复提醒"), this, [this] { setQuietMinutes(0); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("设置"), this, [this] {
        selectPage(4);
        showAndRaise();
    });
    menu->addAction(QStringLiteral("退出"), this, &MainWindow::requestExit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    showAndRaise();
            });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
}
bool MainWindow::trayAvailable() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}
void MainWindow::showAndRaise() {
    showNormal();
    raise();
    activateWindow();
}
void MainWindow::selectPage(int index) {
    ui_->navigation->setCurrentRow(index);
}
void MainWindow::requestExit() {
    quitting_ = true;
    qApp->quit();
}
void MainWindow::closeEvent(QCloseEvent *event) {
    settings_->setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (!quitting_ && settings_->value(QStringLiteral("behavior/closeToTray"), true).toBool() &&
        trayAvailable()) {
        hide();
        event->ignore();
        if (!settings_->value(QStringLiteral("behavior/trayExplained"), false).toBool()) {
            tray_->showMessage(
                QStringLiteral("qDing 仍在运行"),
                QStringLiteral("提醒与番茄钟继续运行。点击托盘可打开，选择“退出”结束程序。"));
            settings_->setValue(QStringLiteral("behavior/trayExplained"), true);
        }
    } else {
        event->accept();
        requestExit();
    }
}
void MainWindow::setQuietMinutes(int minutes) {
    quietRequested_ = minutes != 0;
    quietUntil_ = minutes > 0 ? QDateTime::currentDateTimeUtc().addSecs(minutes * 60) : QDateTime{};
    if (quietUntil_.isValid())
        quietTimer_.start();
    else
        quietTimer_.stop();
    refreshQuiet();
}
void MainWindow::refreshQuiet() {
    if (quietRequested_ && quietUntil_.isValid() &&
        QDateTime::currentDateTimeUtc() >= quietUntil_) {
        quietRequested_ = false;
        quietUntil_ = {};
        quietTimer_.stop();
    }
    {
        QSignalBlocker block(quietBox_);
        quietBox_->setChecked(quietRequested_);
    }
    const bool quiet = quietRequested_ || locked_ || sleeping_;
    notifications_->setQuiet(quiet);
    scheduler_->setQuiet(quiet);
    ui_->quietBadge->setText(quiet ? QStringLiteral("  勿扰 / 暂停展示")
                                   : QStringLiteral("  提醒已开启"));
}
void MainWindow::capturePages(const QString &directory) {
    QDir().mkpath(directory);
    const QStringList names{QStringLiteral("today"), QStringLiteral("reminders"),
                            QStringLiteral("pomodoro"), QStringLiteral("history"),
                            QStringLiteral("settings")};
    for (int i = 0; i < names.size(); ++i) {
        selectPage(i);
        ui_->pages->layout()->activate();
        grab().save(directory + QLatin1Char('/') + names[i] + QStringLiteral(".png"));
    }
    selectPage(0);
}
} // namespace qding
