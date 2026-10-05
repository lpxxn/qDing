#include "services/theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleHints>

namespace qding {
ThemeManager::ThemeManager(QObject *parent) : QObject(parent) {
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (mode_ == QLatin1String("system"))
            apply(mode_, accent_);
    });
}
void ThemeManager::apply(const QString &mode, const QColor &accent) {
    mode_ = mode;
    accent_ = accent.isValid() ? accent : QColor("#e36d57");
    const bool dark = mode == QLatin1String("dark") ||
                      (mode == QLatin1String("system") &&
                       qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    theme_ = dark ? Theme{QColor("#171b23"),
                          QColor("#222833"),
                          QColor("#edf0f7"),
                          QColor("#a4aebe"),
                          QColor("#343c49"),
                          accent_,
                          true}
                  : Theme{QColor("#f4f5f8"),
                          QColor("#ffffff"),
                          QColor("#252c3b"),
                          QColor("#737e91"),
                          QColor("#e2e6ed"),
                          accent_,
                          false};
    QPalette palette;
    palette.setColor(QPalette::Window, theme_.background);
    palette.setColor(QPalette::WindowText, theme_.text);
    palette.setColor(QPalette::Base, theme_.surface);
    palette.setColor(QPalette::AlternateBase, theme_.background);
    palette.setColor(QPalette::Text, theme_.text);
    palette.setColor(QPalette::Button, theme_.surface);
    palette.setColor(QPalette::ButtonText, theme_.text);
    palette.setColor(QPalette::Highlight, theme_.accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, theme_.surface);
    palette.setColor(QPalette::ToolTipText, theme_.text);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme_.muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme_.muted);
    qApp->setPalette(palette);
    // 样式只在主题变化时更新，自绘控件通过 changed 同步；不在计时刷新中重设 QSS。
    qApp->setStyleSheet(QStringLiteral(R"(
        QWidget { font-size: 14px; }
        QMainWindow, QDialog { background: %1; }
        QLabel[role="heading"] { font-size: 28px; font-weight: 700; }
        QLabel[role="muted"] { color: %4; }
        QLabel[role="brand"] { font-size: 24px; font-weight: 700; color: %6; }
        QFrame[role="card"] { background: %2; border: 1px solid %5; border-radius: 12px; }
        QListWidget#navigation { background: %2; border: none; padding: 10px; }
        QListWidget#navigation::item { padding: 13px 16px; border-radius: 8px; margin: 3px 0; }
        QListWidget#navigation::item:selected { background: %6; color: white; }
        QListView#reminderList { background: %2; border: 1px solid %5; border-radius: 12px; outline: none; }
        QPushButton { background: %2; color: %3; border: 1px solid %5; border-radius: 7px; padding: 9px 16px; }
        QPushButton:hover { border-color: %6; }
        QPushButton:pressed { background: %1; }
        QPushButton[primary="true"] { background: %6; color: white; border: 1px solid %6; font-weight: 600; }
        QPushButton:disabled { color: %4; }
        QLineEdit,QTextEdit,QSpinBox,QComboBox,QTimeEdit,QDateTimeEdit {
            background: %2; border: 1px solid %5; border-radius: 6px; padding: 7px; color: %3;
        }
        QLineEdit:focus,QTextEdit:focus { border-color: %6; }
        QTableWidget { background: %2; border: 1px solid %5; gridline-color: %5; }
        QHeaderView::section { background: %1; border: none; padding: 9px; color: %4; }
        QGroupBox { border: 1px solid %5; border-radius: 8px; margin-top: 16px; padding-top: 16px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
        QCheckBox { spacing: 8px; }
        QStatusBar { color: %4; }
    )")
                            .arg(theme_.background.name(), theme_.surface.name(),
                                 theme_.text.name(), theme_.muted.name(), theme_.border.name(),
                                 theme_.accent.name()));
    emit changed();
}
} // namespace qding
