#include "ui/popup.h"
#include "services/media.h"
#include "ui/fireworks.h"
#include "ui_celebrationpopup.h"
#include "ui_reminderpopup.h"
#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QHideEvent>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QMovie>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSoundEffect>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>
#include <spdlog/spdlog.h>

namespace qding {
ReminderPopup::ReminderPopup(Occurrence occurrence, MediaService *media,
                             PopupPresentation presentation, QWidget *parent)
    : QWidget(parent,
              (presentation.style == PopupStyle::Celebration ? Qt::Tool | Qt::FramelessWindowHint
                                                             : Qt::Window) |
                  Qt::WindowStaysOnTopHint),
      occurrence_(std::move(occurrence)), presentation_(presentation) {
    setWindowTitle(QStringLiteral("qDing · ") + occurrence_.title);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    const bool celebration = presentation.style == PopupStyle::Celebration;
    QLabel *image = nullptr;
    QComboBox *snooze = nullptr;
    QPushButton *done = nullptr, *later = nullptr, *ignore = nullptr;
    // 两个独立 Designer 表单共享绑定与 action 协议，保证完成/稍后/历史行为相同。
    auto bind = [&](auto &form) {
        form.setupUi(this);
        form.titleLabel->setText(occurrence_.title);
        form.bodyLabel->setPlainText(occurrence_.body);
        form.timeLabel->setText(occurrence_.scheduledAt.toLocalTime().toString(
            QStringLiteral("计划时间  MM-dd HH:mm")));
        image = form.imageLabel;
        snooze = form.snoozeDuration;
        done = form.completeButton;
        later = form.snoozeButton;
        ignore = form.dismissButton;
        connect(form.silenceButton, &QPushButton::clicked, this, &ReminderPopup::silenceRequested);
    };
    if (celebration) {
        setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
        setAttribute(Qt::WA_MacAlwaysShowToolWindow); // 应用在后台时也展示提醒浮层。
#endif
        Ui::CelebrationPopup form;
        bind(form);
        // 浮层有自身夜色配色；用户正文与图片仍走相同的纯文本/本地媒体路径。
        auto palette = this->palette();
        palette.setColor(QPalette::Window, QColor("#171d2e"));
        palette.setColor(QPalette::Base, QColor("#171d2e"));
        palette.setColor(QPalette::Text, QColor("#c5cee2"));
        setPalette(palette);
        auto *shadow = new QGraphicsDropShadowEffect(form.celebrationCard);
        shadow->setBlurRadius(30);
        shadow->setOffset(0, 6);
        shadow->setColor(QColor(0, 0, 0, 100));
        form.celebrationCard->setGraphicsEffect(shadow);
        form.fireworks->setAnimationEnabled(presentation.animations);
        form.detailsLayout->setAlignment(Qt::AlignTop);
        actions_.append(form.closeButton);
        connect(form.closeButton, &QPushButton::clicked, this,
                [this] { request(QStringLiteral("dismissed")); });
        setMinimumWidth(520);
        setMaximumWidth(560);
        resize(540, 560);
    } else {
        Ui::ReminderPopup form;
        bind(form);
        setMinimumWidth(430);
        setMaximumWidth(550);
        resize(430, 300);
    }
    // setupUi 的 windowTitle 属性是设计时占位，运行时始终显示事项标题。
    setWindowTitle(QStringLiteral("qDing · ") + occurrence_.title);
    const auto path = media->resolve(occurrence_.visual);
    if (!occurrence_.visual.isEmpty() && path.isEmpty())
        spdlog::warn("visual asset missing occurrence={}", occurrence_.id.toStdString());
    image->setMaximumSize(380, 260);
    image->setVisible(!path.isEmpty());
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().compare(QStringLiteral("gif"), Qt::CaseInsensitive) == 0) {
            auto *movie = new QMovie(path, QByteArray{}, image);
            movie->setCacheMode(QMovie::CacheNone);
            // 第一次解码前设置缩放尺寸，避免首帧按原始尺寸缓存。
            QImageReader reader(path);
            movie->setScaledSize(
                reader.size().scaled(360, celebration ? 180 : 240, Qt::KeepAspectRatio));
            connect(movie, &QMovie::error, this, [image](QImageReader::ImageReaderError) {
                image->hide();
                spdlog::warn("GIF decoding failed; keeping text reminder");
            });
            image->setMovie(movie);
            movie->start();
        } else {
            QPixmap pix(path);
            image->setPixmap(pix.scaled(360, celebration ? 180 : 240, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
        }
    }
    for (int i = 0; i < 3; ++i)
        snooze->setItemData(i, (i + 1) * 5);
    actions_.append({done, later, ignore});
    connect(done, &QPushButton::clicked, this, [this] { request(QStringLiteral("completed")); });
    connect(ignore, &QPushButton::clicked, this, [this] { request(QStringLiteral("dismissed")); });
    connect(later, &QPushButton::clicked, this,
            [this, snooze] { request(QStringLiteral("snoozed"), snooze->currentData().toInt()); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] { request(QStringLiteral("dismissed")); });
    adjustSize();
}
void ReminderPopup::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (presented_)
        return;
    presented_ = true;
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const auto available = screen->availableGeometry();
    const int maxWidth = qMax(240, available.width() - 32);
    if (minimumWidth() > maxWidth)
        setMinimumWidth(maxWidth);
    setMaximumWidth(qMin(maximumWidth(), maxWidth));
    if (presentation_.style == PopupStyle::Celebration) {
        // 给小屏保留完整操作区，正文/自定义媒体在独立滚动区域内阅读。
        const int budget = qMax(320, available.height() - 32);
        const int heroHeight = qBound(64, budget - 370, 146);
        findChild<FireworksWidget *>()->setFixedHeight(heroHeight);
        auto *body = findChild<QTextEdit *>(QStringLiteral("bodyLabel"));
        body->document()->setDocumentMargin(0);
        body->document()->setTextWidth(body->viewport()->width());
        const int bodyHeight = qBound(42, qCeil(body->document()->size().height()) + 8, 90);
        body->setFixedHeight(bodyHeight);
        const int titleHeight = qMax(32, findChild<QLabel *>(QStringLiteral("titleLabel"))
                                             ->heightForWidth(body->viewport()->width()));
        const int desired =
            occurrence_.visual.isEmpty() ? qBound(136, titleHeight + bodyHeight + 50, 220) : 280;
        findChild<QScrollArea *>(QStringLiteral("contentScroll"))
            ->setFixedHeight(qBound(80, budget - 290 - heroHeight, desired));
        setMaximumHeight(qMax(320, available.height() - 32));
    }
    adjustSize();
    const auto size = frameGeometry().size();
    QPoint target = presentation_.centered
                        ? available.topLeft() + QPoint((available.width() - size.width()) / 2,
                                                       (available.height() - size.height()) / 2)
                        : available.bottomRight() - QPoint(size.width() + 23, size.height() + 23);
    target.setX(qMax(available.left(), qMin(target.x(), available.right() - size.width() + 1)));
    target.setY(qMax(available.top(), qMin(target.y(), available.bottom() - size.height() + 1)));
    move(target);
    if (presentation_.style != PopupStyle::Celebration || !presentation_.animations)
        return;
    auto *entrance = new QParallelAnimationGroup(this);
    auto *position = new QPropertyAnimation(this, "pos", entrance);
    position->setDuration(300);
    position->setStartValue(target + QPoint(0, 12));
    position->setEndValue(target);
    position->setEasingCurve(QEasingCurve::OutCubic);
    // offscreen/minimal 不支持窗口透明度，只跳过淡入，烟花与布局仍可检查。
    if (QGuiApplication::platformName() != QLatin1String("offscreen") &&
        QGuiApplication::platformName() != QLatin1String("minimal")) {
        auto *opacity = new QPropertyAnimation(this, "windowOpacity", entrance);
        opacity->setDuration(240);
        opacity->setStartValue(0.0);
        opacity->setEndValue(1.0);
    }
    entrance->start(QAbstractAnimation::DeleteWhenStopped);
}
void ReminderPopup::mousePressEvent(QMouseEvent *event) {
    if (presentation_.style == PopupStyle::Celebration && event->button() == Qt::LeftButton &&
        event->position().y() < 90 && windowHandle() && windowHandle()->startSystemMove()) {
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
void ReminderPopup::hideEvent(QHideEvent *event) {
    for (auto *animation :
         findChildren<QParallelAnimationGroup *>(QString{}, Qt::FindDirectChildrenOnly))
        animation->stop();
    if (windowOpacity() != 1.0)
        setWindowOpacity(1.0);
    QWidget::hideEvent(event);
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
    active_ = new ReminderPopup(o, media_, presentation_);
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
