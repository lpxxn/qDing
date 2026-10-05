#pragma once
#include "domain/types.h"
#include <QWidget>
namespace qding {
class ThemeManager;
class CountdownRing final : public QWidget {
public:
    explicit CountdownRing(QWidget *parent = nullptr);
    void setTheme(ThemeManager *theme);
    void setSnapshot(PomodoroSnapshot snapshot);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    ThemeManager *theme_ = nullptr;
    PomodoroSnapshot state_;
};
} // namespace qding
