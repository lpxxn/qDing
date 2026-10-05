#pragma once
#include "domain/types.h"
#include <QPointer>
#include <QQueue>
#include <QWidget>
class QMovie;
class QPushButton;
class QCloseEvent;
class QSoundEffect;
class QMediaPlayer;
class QAudioOutput;
class QTimer;

namespace qding {
class MediaService;
class ReminderPopup final : public QWidget {
    Q_OBJECT
public:
    ReminderPopup(Occurrence occurrence, MediaService *media);
    void dismissWithoutAction();
    void retry();
    const Occurrence &occurrence() const { return occurrence_; }
signals:
    void action(QString id, QString status, int snoozeMinutes);
    void silenceRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void request(const QString &status, int snooze = 0);
    Occurrence occurrence_;
    QList<QPushButton *> actions_;
    bool closing_ = false, waiting_ = false;
};

class NotificationCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit NotificationCoordinator(MediaService *media, QObject *parent = nullptr);
    ~NotificationCoordinator() override;
    void enqueue(OccurrenceList list);
    void preview(const Reminder &reminder, bool silent = false);
    void acknowledge(const Occurrence &occurrence);
    void cancelReminder(const QString &id);
    void setQuiet(bool quiet);
    void setMuted(bool muted);
    void setGlobalVolume(int volume);
    void stopSound();
    void retry();
signals:
    void displayed(QString id);
    void handled(QString id, QString status, int snoozeMinutes);

private:
    void showNext();
    void playSound(const Occurrence &occurrence);
    MediaService *media_;
    QQueue<Occurrence> queue_;
    QPointer<ReminderPopup> active_;
    QSoundEffect *effect_;
    QMediaPlayer *player_;
    QAudioOutput *audio_;
    QTimer *soundLimit_;
    bool quiet_ = false, muted_ = false;
    bool soundActive_ = false;
    int globalVolume_ = 100;
};
} // namespace qding
