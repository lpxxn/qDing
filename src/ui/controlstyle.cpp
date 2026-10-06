#include "ui/controlstyle.h"
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QResource>
#include <QStyleOption>
#include <QStyledItemDelegate>

// 静态库中的 qrc 要显式引用，防止链接器将未引用的资源对象移除。
// Q_INIT_RESOURCE 必须从全局命名空间调用。
static void initializeControlResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(controls);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace qding {
namespace {
QColor blend(const QColor &foreground, const QColor &background, qreal amount) {
    return QColor::fromRgbF(background.redF() * (1 - amount) + foreground.redF() * amount,
                            background.greenF() * (1 - amount) + foreground.greenF() * amount,
                            background.blueF() * (1 - amount) + foreground.blueF() * amount);
}

// 不采用系统菜单式 delegate：它会给每一行画方框，并忽略圆角与留白。
// 使用应用调色板，因此主题/强调色改变时，无需重新创建选项或 delegate。
class ComboItemDelegate final : public QStyledItemDelegate {
public:
    explicit ComboItemDelegate(QComboBox *combo) : QStyledItemDelegate(combo), combo_(combo) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setWidth(size.width() + 48);
        size.setHeight(qMax(36, option.fontMetrics.height() + 16));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const auto palette = QApplication::palette();
        const QColor surface = palette.color(QPalette::Base);
        const QColor accent = palette.color(QPalette::Highlight);
        const bool enabled = item.state.testFlag(QStyle::State_Enabled);
        const bool active = enabled && (item.state.testFlag(QStyle::State_Selected) ||
                                        item.state.testFlag(QStyle::State_MouseOver));
        const bool chosen =
            index.row() == combo_->currentIndex() && index.column() == combo_->modelColumn();
        const QRectF row = QRectF(item.rect).adjusted(0, 1, 0, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        if (active || chosen) {
            painter->setBrush(blend(accent, surface, active ? 0.14 : 0.07));
            painter->drawRoundedRect(row, 7, 7);
        }
        painter->setFont(item.font);
        painter->setPen(
            palette.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::Text));
        QRect textRect = row.toRect().adjusted(12, 0, -34, 0);
        if (!item.icon.isNull()) {
            item.icon.paint(painter, QRect(textRect.left(), textRect.center().y() - 8, 16, 16));
            textRect.setLeft(textRect.left() + 24);
        }
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          item.fontMetrics.elidedText(item.text, Qt::ElideRight, textRect.width()));
        if (chosen) {
            const QPointF center(row.right() - 17, row.center().y());
            painter->setBrush(Qt::NoBrush);
            painter->setPen(
                QPen(enabled ? accent : palette.color(QPalette::Disabled, QPalette::Text), 1.8,
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath check;
            check.moveTo(center + QPointF(-4, 0));
            check.lineTo(center + QPointF(-1, 3));
            check.lineTo(center + QPointF(5, -3));
            painter->drawPath(check);
        }
        painter->restore();
    }

private:
    QComboBox *combo_; // delegate 由 combo 持有，生命周期不会超过它。
};

class ControlStyle final : public QProxyStyle {
public:
    ControlStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override {
        if (hint == SH_ComboBox_Popup)
            return 0; // 两个平台都使用可绘制的列表，而不是系统菜单式下拉框。
        if (hint == SH_ComboBox_PopupFrameStyle)
            return QFrame::NoFrame;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void polish(QWidget *widget) override {
        QProxyStyle::polish(widget);
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            // QSS 重设主题时也会 polish；避免反复替换 delegate。
            if (!dynamic_cast<ComboItemDelegate *>(combo->itemDelegate()))
                combo->setItemDelegate(new ComboItemDelegate(combo));
            auto *view = combo->view();
            view->setMouseTracking(true);
            view->setFrameShape(QFrame::NoFrame);
            view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            if (auto *list = qobject_cast<QListView *>(view))
                list->setUniformItemSizes(true);
            auto *popup = view->window();
            if (popup != combo->window()) {
                popup->setObjectName(QStringLiteral("qdingComboPopup"));
                popup->setAttribute(Qt::WA_TranslucentBackground);
            }
        }
    }
};
} // namespace

void installControlStyle() {
    initializeControlResources();
    QApplication::setStyle(new ControlStyle);
}
} // namespace qding
