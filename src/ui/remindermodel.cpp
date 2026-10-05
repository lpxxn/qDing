#include "ui/remindermodel.h"
#include "services/theme.h"
#include <QPainter>

namespace qding {
int ReminderListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(list_.size());
}
QVariant ReminderListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= list_.size())
        return {};
    const auto &r = list_[index.row()];
    if (role == ReminderRole)
        return QVariant::fromValue(r);
    if (role == Qt::DisplayRole)
        return r.title + QLatin1Char(' ') + r.body;
    if (role == Qt::ToolTipRole)
        return r.body;
    return {};
}
void ReminderListModel::replace(ReminderList list) {
    beginResetModel();
    list_ = std::move(list);
    endResetModel();
}
Reminder ReminderListModel::at(int row) const {
    return row >= 0 && row < list_.size() ? list_[row] : Reminder{};
}
ReminderDelegate::ReminderDelegate(ThemeManager *theme, QObject *parent)
    : QStyledItemDelegate(parent), theme_(theme) {}
void ReminderDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    const auto r = index.data(ReminderListModel::ReminderRole).value<Reminder>();
    const auto &theme = theme_->theme();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const auto rect = option.rect.adjusted(8, 4, -8, -4);
    if (option.state & QStyle::State_Selected) {
        auto color = theme.accent;
        color.setAlpha(22);
        p->setBrush(color);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(rect, 9, 9);
    }
    p->setBrush(r.enabled ? theme.accent : theme.muted);
    p->setPen(Qt::NoPen);
    p->drawEllipse(QPointF(rect.left() + 22, rect.center().y()), 5, 5);
    QFont title = option.font;
    title.setPointSize(12);
    title.setBold(true);
    p->setFont(title);
    p->setPen(theme.text);
    const auto textRect = rect.adjusted(44, 12, -100, -12);
    p->drawText(textRect, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(r.title, Qt::ElideRight, textRect.width()));
    QStringList rules;
    for (const auto &rule : r.rules)
        rules << ruleSummary(rule);
    p->setFont(option.font);
    p->setPen(theme.muted);
    p->drawText(textRect.adjusted(0, 30, 0, 0), Qt::AlignTop,
                p->fontMetrics().elidedText(rules.join(QStringLiteral(" · ")), Qt::ElideRight,
                                            textRect.width()));
    p->drawText(QRect(rect.right() - 90, rect.top(), 80, rect.height()), Qt::AlignCenter,
                r.enabled ? QStringLiteral("已开启") : QStringLiteral("已暂停"));
    p->setPen(theme.border);
    p->drawLine(rect.bottomLeft() + QPoint(44, 0), rect.bottomRight() - QPoint(16, 0));
    p->restore();
}
} // namespace qding
