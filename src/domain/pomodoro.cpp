#include "domain/pomodoro.h"
#include <algorithm>

namespace qding {
PomodoroEngine::PomodoroEngine(QObject *parent) : QObject(parent) {
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] {
        const auto elapsed = clock_.elapsed();
        // 事件循环停滞/休眠后保持暂停，不能跨越数个阶段追赶循环。
        if (elapsed > 120000) {
            clock_.restart();
            pause();
            return;
        }
        advance(elapsed);
    });
}

PomodoroSnapshot PomodoroEngine::snapshot() const {
    auto result = state_;
    if (result.status == PomodoroStatus::Running && clock_.isValid())
        result.remainingMs = qMax<qint64>(0, result.remainingMs - clock_.elapsed());
    return result;
}

void PomodoroEngine::restore(PomodoroSnapshot s) {
    timer_.stop();
    clock_.invalidate();
    s.workMinutes = std::clamp(s.workMinutes, 1, 240);
    s.breakMinutes = std::clamp(s.breakMinutes, 1, 120);
    if (s.status == PomodoroStatus::Running)
        s.status = PomodoroStatus::Paused;
    s.remainingMs = std::clamp<qint64>(s.remainingMs, 0, 240LL * 60000);
    s.phaseDurationMs =
        qMax(s.remainingMs, std::clamp<qint64>(s.phaseDurationMs, 1, 240LL * 60000));
    state_ = s;
    publish(false);
}

void PomodoroEngine::configure(int work, int rest) {
    state_.workMinutes = std::clamp(work, 1, 240);
    state_.breakMinutes = std::clamp(rest, 1, 120);
    if (state_.status == PomodoroStatus::Idle) {
        state_.remainingMs = state_.workMinutes * 60000LL;
        state_.phaseDurationMs = state_.remainingMs;
    }
    publish(true);
}

void PomodoroEngine::startOrResume() {
    if (state_.status == PomodoroStatus::Running)
        return;
    if (state_.status == PomodoroStatus::Idle) {
        state_.phase = PomodoroPhase::Work;
        state_.remainingMs = state_.workMinutes * 60000LL;
        state_.phaseDurationMs = state_.remainingMs;
        state_.rounds = 0;
    }
    state_.status = PomodoroStatus::Running;
    clock_.start();
    timer_.start();
    publish(true);
}

void PomodoroEngine::pause() {
    if (state_.status != PomodoroStatus::Running)
        return;
    state_ = snapshot();
    state_.status = PomodoroStatus::Paused;
    timer_.stop();
    clock_.invalidate();
    publish(true);
}

void PomodoroEngine::stop() {
    timer_.stop();
    clock_.invalidate();
    state_.status = PomodoroStatus::Idle;
    state_.phase = PomodoroPhase::Work;
    state_.remainingMs = state_.workMinutes * 60000LL;
    state_.phaseDurationMs = state_.remainingMs;
    publish(true);
}

void PomodoroEngine::advance(qint64 elapsedMs) {
    if (state_.status != PomodoroStatus::Running || elapsedMs < 0)
        return;
    // advance 仅消费给定时间，测试可以直接推进；实时入口每次重置单调时间锚点。
    clock_.restart();
    state_.remainingMs -= elapsedMs;
    bool transitioned = false;
    if (state_.remainingMs <= 0) {
        const auto finished = state_.phase;
        if (finished == PomodoroPhase::Work)
            ++state_.rounds;
        state_.phase = finished == PomodoroPhase::Work ? PomodoroPhase::Break : PomodoroPhase::Work;
        state_.remainingMs =
            (state_.phase == PomodoroPhase::Work ? state_.workMinutes : state_.breakMinutes) *
            60000LL;
        state_.phaseDurationMs = state_.remainingMs;
        transitioned = true;
        emit phaseFinished(finished);
    }
    lastCheckpoint_ += elapsedMs;
    publish(transitioned || lastCheckpoint_ >= 30000);
}

void PomodoroEngine::publish(bool persist) {
    emit changed(snapshot());
    if (persist) {
        lastCheckpoint_ = 0;
        emit checkpoint(snapshot());
    }
}
} // namespace qding
