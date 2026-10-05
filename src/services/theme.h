#pragma once
#include <QColor>
#include <QObject>
namespace qding {
struct Theme {
    QColor background, surface, text, muted, border, accent;
    bool dark = false;
};
class ThemeManager final : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);
    void apply(const QString &mode, const QColor &accent = QColor("#e36d57"));
    const Theme &theme() const { return theme_; }
signals:
    void changed();

private:
    Theme theme_;
    QString mode_ = QStringLiteral("system");
    QColor accent_ = QColor("#e36d57");
};
} // namespace qding
