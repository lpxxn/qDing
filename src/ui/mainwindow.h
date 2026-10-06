#pragma once
#include "domain/types.h"
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <memory>
class QSettings;
class QSystemTrayIcon;
class QListView;
class QSortFilterProxyModel;
class QTableWidget;
class QLabel;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QCloseEvent;
namespace Ui {
class MainWindow;
}

namespace qding {
class StorageService;
class MediaService;
class ThemeManager;
class PomodoroEngine;
class ReminderScheduler;
class PlatformIntegration;
class NotificationCoordinator;
class ReminderListModel;
class ReminderPopup;
class CountdownRing;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(StorageService *storage, MediaService *media, ThemeManager *theme,
               PomodoroEngine *pomodoro, ReminderScheduler *scheduler,
               PlatformIntegration *platform, NotificationCoordinator *notifications,
               QSettings *settings);
    ~MainWindow() override;
    void showAndRaise();
    void selectPage(int index);
    void setQuietMinutes(int minutes);
    void requestExit();
    bool trayAvailable() const;
    void capturePages(const QString &directory);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createToday();
    QWidget *createReminders();
    QWidget *createPomodoro();
    QWidget *createHistory();
    QWidget *createSettings();
    void editReminder(bool fresh = false);
    Reminder selectedReminder() const;
    bool hasSelection() const;
    void updateReminders(ReminderList list);
    void updatePomodoro(PomodoroSnapshot state);
    void updateToday();
    void refreshQuiet();
    void createTray();
    std::unique_ptr<Ui::MainWindow> ui_;
    StorageService *storage_;
    MediaService *media_;
    ThemeManager *theme_;
    PomodoroEngine *pomodoro_;
    ReminderScheduler *scheduler_;
    PlatformIntegration *platform_;
    NotificationCoordinator *notifications_;
    QSettings *settings_;
    ReminderListModel *model_;
    QSortFilterProxyModel *proxy_;
    QListView *list_;
    QSystemTrayIcon *tray_ = nullptr;
    QTableWidget *history_;
    QLabel *nextLabel_, *countLabel_, *roundsLabel_, *emptyLabel_, *phaseLabel_;
    CountdownRing *ring_;
    QPushButton *startButton_;
    QSpinBox *workMinutes_, *breakMinutes_;
    QCheckBox *quietBox_;
    QTimer quietTimer_;
    QDateTime quietUntil_;
    QPointer<ReminderPopup> popupPreview_;
    bool quietRequested_ = false, locked_ = false, sleeping_ = false, quitting_ = false,
         ready_ = false;
};
} // namespace qding
