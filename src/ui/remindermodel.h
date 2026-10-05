#pragma once
#include "domain/types.h"
#include <QAbstractListModel>
#include <QStyledItemDelegate>

namespace qding {
class ThemeManager;
class ReminderListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum { ReminderRole = Qt::UserRole + 1 };
    explicit ReminderListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    void replace(ReminderList list);
    Reminder at(int row) const;
    const ReminderList &reminders() const { return list_; }

private:
    ReminderList list_;
};
class ReminderDelegate final : public QStyledItemDelegate {
public:
    explicit ReminderDelegate(ThemeManager *theme, QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return {400, 100};
    }

private:
    ThemeManager *theme_;
};
} // namespace qding
