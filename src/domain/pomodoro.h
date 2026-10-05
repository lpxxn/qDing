#pragma once
#include "domain/types.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace qding {
// 计时状态不依赖窗口；关闭主窗口不会销毁引擎。
class PomodoroEngine final : public QObject {
    Q_OBJECT
public:
    explicit PomodoroEngine(QObject *parent = nullptr);
    PomodoroSnapshot snapshot() const;
    void restore(PomodoroSnapshot snapshot);
    void configure(int workMinutes, int breakMinutes);
    void startOrResume();
    void pause();
    void stop();
    // 测试入口也用于 timeout：传入实际经过时长，不按回调次数减秒。
    void advance(qint64 elapsedMs);
signals:
    void changed(qding::PomodoroSnapshot snapshot);
    void checkpoint(qding::PomodoroSnapshot snapshot);
    void phaseFinished(qding::PomodoroPhase finished);

private:
    void publish(bool persist);
    PomodoroSnapshot state_;
    QElapsedTimer clock_;
    QTimer timer_;
    qint64 lastCheckpoint_ = 0;
};
} // namespace qding
Q_DECLARE_METATYPE(qding::PomodoroPhase)
