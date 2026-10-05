#include "ui/popup.h"
#include "services/media.h"
#include "ui_reminderpopup.h"
#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QMovie>
#include <QPushButton>
#include <QScreen>
#include <QSoundEffect>
#include <QTimer>
#include <QVBoxLayout>
#include <spdlog/spdlog.h>

namespace qding {
ReminderPopup::ReminderPopup(Occurrence occurrence, MediaService *media)
    : QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint), occurrence_(std::move(occurrence)) {
    setWindowTitle(QStringLiteral("qDing · ") + occurrence_.title);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMinimumWidth(430);
    setMaximumWidth(550);
    resize(430, 300);
    Ui::ReminderPopup form;
    form.setupUi(this);
    form.titleLabel->setText(occurrence_.title);
    form.bodyLabel->setPlainText(occurrence_.body);
    form.timeLabel->setText(
        occurrence_.scheduledAt.toLocalTime().toString(QStringLiteral("计划时间  MM-dd HH:mm")));
    const auto path = media->resolve(occurrence_.visual);
    if (!occurrence_.visual.isEmpty() && path.isEmpty())
        spdlog::warn("visual asset missing occurrence={}", occurrence_.id.toStdString());
    auto *image = form.imageLabel;
    image->setMaximumSize(380, 260);
    image->setVisible(!path.isEmpty());
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().compare(QStringLiteral("gif"), Qt::CaseInsensitive) == 0) {
            auto *movie = new QMovie(path, QByteArray{}, image);
            movie->setCacheMode(QMovie::CacheNone);
            // 第一次解码前设置缩放尺寸，避免首帧按原始尺寸缓存。
            QImageReader reader(path);
            movie->setScaledSize(reader.size().scaled(360, 240, Qt::KeepAspectRatio));
            connect(movie, &QMovie::error, this, [image](QImageReader::ImageReaderError) {
                image->hide();
                spdlog::warn("GIF decoding failed; keeping text reminder");
            });
            image->setMovie(movie);
            movie->start();
        } else {
            QPixmap pix(path);
            image->setPixmap(pix.scaled(360, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    auto *snooze = form.snoozeDuration;
    for (int i = 0; i < 3; ++i)
        snooze->setItemData(i, (i + 1) * 5);
    auto *done = form.completeButton;
    auto *later = form.snoozeButton;
    auto *ignore = form.dismissButton;
    actions_ = {done, later, ignore};
    connect(done, &QPushButton::clicked, this, [this] { request(QStringLiteral("completed")); });
    connect(ignore, &QPushButton::clicked, this, [this] { request(QStringLiteral("dismissed")); });
    connect(later, &QPushButton::clicked, this,
            [this, snooze] { request(QStringLiteral("snoozed"), snooze->currentData().toInt()); });
    connect(form.silenceButton, &QPushButton::clicked, this, &ReminderPopup::silenceRequested);
    adjustSize();
    const auto geometry = QApplication::primaryScreen()->availableGeometry();
    move(geometry.right() - width() - 24, geometry.bottom() - height() - 24);
}
void ReminderPopup::request(const QString &status, int snooze) {
    if (waiting_)
        return;
    waiting_ = true;
    for (auto *button : actions_)
        button->setEnabled(false);
    emit action(occurrence_.id, status, snooze);
}
void ReminderPopup::closeEvent(QCloseEvent *event) {
    if (closing_)
        event->accept();
    else {
        event->ignore();
        request(QStringLiteral("dismissed"));
    }
}
void ReminderPopup::dismissWithoutAction() {
    closing_ = true;
    close();
}
void ReminderPopup::retry() {
    waiting_ = false;
    for (auto *button : actions_)
        button->setEnabled(true);
}

NotificationCoordinator::NotificationCoordinator(MediaService *media, QObject *parent)
    : QObject(parent), media_(media) {
    effect_ = new QSoundEffect(this);
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    player_->setAudioOutput(audio_);
    soundLimit_ = new QTimer(this);
    soundLimit_->setSingleShot(true);
    connect(soundLimit_, &QTimer::timeout, this, &NotificationCoordinator::stopSound);
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) {
                spdlog::warn("custom audio failed: {}", message.toStdString());
                if (soundActive_ && active_ && !quiet_ && !muted_) {
                    effect_->setSource(QUrl::fromLocalFile(media_->defaultSound()));
                    effect_->play();
                }
            });
    connect(effect_, &QSoundEffect::statusChanged, this, [this] {
        if (effect_->status() == QSoundEffect::Error) {
            spdlog::warn("WAV sound unavailable");
            const auto fallback = QUrl::fromLocalFile(media_->defaultSound());
            if (soundActive_ && active_ && !quiet_ && !muted_ && effect_->source() != fallback) {
                effect_->setSource(fallback);
                effect_->play();
            }
        }
    });
}
NotificationCoordinator::~NotificationCoordinator() {
    stopSound();
    if (active_)
        delete active_.data();
}
void NotificationCoordinator::enqueue(OccurrenceList list) {
    for (auto &o : list) {
        if (quiet_) {
            emit handled(o.id, QStringLiteral("deferred"), 0);
            continue;
        }
        queue_.enqueue(std::move(o));
    }
    showNext();
}
void NotificationCoordinator::preview(const Reminder &r, bool silent) {
    Occurrence o;
    o.id = QStringLiteral("preview:") + newId();
    o.title = r.title.isEmpty() ? QStringLiteral("提醒预览") : r.title;
    o.body = r.body;
    o.visual = r.visual;
    o.sound = r.sound;
    o.volume = silent ? 0 : r.volume;
    o.scheduledAt = QDateTime::currentDateTimeUtc();
    enqueue({o});
}
void NotificationCoordinator::showNext() {
    if (active_ || quiet_ || queue_.isEmpty())
        return;
    const auto o = queue_.dequeue();
    active_ = new ReminderPopup(o, media_);
    connect(active_, &ReminderPopup::action, this,
            [this](const QString &id, const QString &status, int minutes) {
                stopSound();
                if (id.startsWith(QStringLiteral("preview:"))) {
                    auto o = active_->occurrence();
                    o.status = status;
                    acknowledge(o);
                } else
                    emit handled(id, status, minutes);
            });
    connect(active_, &ReminderPopup::silenceRequested, this, &NotificationCoordinator::stopSound);
    active_->show();
    playSound(o);
    if (!o.id.startsWith(QStringLiteral("preview:")))
        emit displayed(o.id);
    spdlog::info("popup shown occurrence={}", o.id.toStdString());
}
void NotificationCoordinator::acknowledge(const Occurrence &o) {
    if (o.status == QLatin1String("shown"))
        return;
    if (active_ && active_->occurrence().id == o.id) {
        active_->dismissWithoutAction();
        active_.clear();
        stopSound();
        QTimer::singleShot(0, this, [this] { showNext(); });
    }
}
void NotificationCoordinator::cancelReminder(const QString &id) {
    QQueue<Occurrence> keep;
    while (!queue_.isEmpty()) {
        auto o = queue_.dequeue();
        if (o.reminderId != id)
            keep.enqueue(o);
    }
    queue_ = std::move(keep);
    if (active_ && active_->occurrence().reminderId == id) {
        active_->dismissWithoutAction();
        active_.clear();
        stopSound();
        showNext();
    }
}
void NotificationCoordinator::setQuiet(bool value) {
    quiet_ = value;
    if (quiet_) {
        if (active_) {
            const auto o = active_->occurrence();
            if (!o.id.startsWith(QStringLiteral("preview:")))
                emit handled(o.id, QStringLiteral("deferred"), 0);
            active_->dismissWithoutAction();
            active_.clear();
        }
        while (!queue_.isEmpty()) {
            const auto o = queue_.dequeue();
            if (!o.id.startsWith(QStringLiteral("preview:")))
                emit handled(o.id, QStringLiteral("deferred"), 0);
        }
        stopSound();
    } else
        showNext();
}
void NotificationCoordinator::setMuted(bool value) {
    muted_ = value;
    if (value)
        stopSound();
}
void NotificationCoordinator::setGlobalVolume(int value) {
    globalVolume_ = qBound(0, value, 100);
}
void NotificationCoordinator::stopSound() {
    soundActive_ = false;
    effect_->stop();
    player_->stop();
    soundLimit_->stop();
}
void NotificationCoordinator::retry() {
    if (active_)
        active_->retry();
}
void NotificationCoordinator::playSound(const Occurrence &o) {
    if (muted_ || quiet_ || o.volume == 0 || globalVolume_ == 0)
        return;
    stopSound();
    const float volume = (o.volume / 100.0f) * (globalVolume_ / 100.0f);
    effect_->setVolume(volume);
    audio_->setVolume(volume);
    auto path = media_->resolve(o.sound);
    if (path.isEmpty())
        path = media_->defaultSound();
    if (path.isEmpty())
        return;
    soundActive_ = true;
    if (QFileInfo(path).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
        effect_->setSource(QUrl::fromLocalFile(path));
        effect_->play();
    } else {
        player_->setSource(QUrl::fromLocalFile(path));
        player_->play();
    }
    soundLimit_->start(30000);
}
} // namespace qding
