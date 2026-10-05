#include "ui/remindereditor.h"
#include "domain/schedule.h"
#include "services/media.h"
#include "storage/storage.h"
#include "ui_remindereditor.h"
#include "ui_ruleeditor.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStyle>
#include <QTimeZone>

namespace qding {
// 动态时间规则的每一行也是 .ui；编辑器只负责数值与信号绑定。
class RuleEditor final : public QFrame {
public:
    RuleEditor(ScheduleRule rule, QWidget *parent = nullptr)
        : QFrame(parent), original_(std::move(rule)) {
        ui_.setupUi(this);
        ui_.timeEdit->setTime(original_.time);
        ui_.onceEdit->setDateTime(original_.onceAt.isValid()
                                      ? original_.onceAt.toLocalTime()
                                      : QDateTime::currentDateTime().addSecs(300));
        days_ = {ui_.day0, ui_.day1, ui_.day2, ui_.day3, ui_.day4, ui_.day5, ui_.day6};
        for (int i = 0; i < 7; ++i)
            days_[i]->setChecked(original_.weekdays & (1 << i));
        connect(ui_.kind, &QComboBox::currentIndexChanged, this,
                [this](int i) { updateVisibility(i); });
        const int index = original_.kind == RuleKind::Once    ? 3
                          : original_.kind == RuleKind::Daily ? 0
                          : original_.weekdays == 31          ? 1
                                                              : 2;
        ui_.kind->setCurrentIndex(index);
        updateVisibility(index);
    }
    QPushButton *removeButton() const { return ui_.removeRule; }
    ScheduleRule value() const {
        auto rule = original_;
        const int index = ui_.kind->currentIndex();
        rule.kind = index == 3 ? RuleKind::Once : index == 0 ? RuleKind::Daily : RuleKind::Weekly;
        rule.time = QTime(ui_.timeEdit->time().hour(), ui_.timeEdit->time().minute());
        const auto datetime = ui_.onceEdit->dateTime();
        rule.onceAt =
            QDateTime(datetime.date(), QTime(datetime.time().hour(), datetime.time().minute()),
                      datetime.timeZone())
                .toUTC();
        if (rule.kind != RuleKind::Once)
            rule.onceAt = {};
        rule.weekdays = index == 0 ? 127 : index == 1 ? 31 : 0;
        if (index == 2)
            for (int i = 0; i < days_.size(); ++i)
                if (days_[i]->isChecked())
                    rule.weekdays |= (1 << i);
        return rule;
    }

private:
    void updateVisibility(int i) {
        ui_.timeEdit->setVisible(i != 3);
        ui_.onceEdit->setVisible(i == 3);
        ui_.daysWidget->setVisible(i == 2);
    }
    Ui::RuleEditor ui_;
    ScheduleRule original_;
    QList<QCheckBox *> days_;
};

ReminderEditor::ReminderEditor(Reminder reminder, StorageService *storage, MediaService *media,
                               QWidget *parent)
    : QDialog(parent), ui_(std::make_unique<Ui::ReminderEditor>()), original_(std::move(reminder)),
      storage_(storage), media_(media), visual_(original_.visual), sound_(original_.sound) {
    ui_->setupUi(this);
    setWindowTitle(original_.title.isEmpty() ? QStringLiteral("新建提醒")
                                             : QStringLiteral("编辑提醒"));
    resize(700, 780);
    ui_->heading->setText(windowTitle());
    title_ = ui_->titleEdit;
    body_ = ui_->bodyEdit;
    enabled_ = ui_->enabledCheck;
    title_->setText(original_.title);
    body_->setPlainText(original_.body);
    enabled_->setChecked(original_.enabled);
    rulesLayout_ = ui_->rulesLayout;
    rulesLayout_->setAlignment(Qt::AlignTop);
    ui_->rulesScroll->setMinimumHeight(150);
    for (const auto &rule : original_.rules)
        addRule(rule);
    if (rules_.isEmpty())
        addRule();
    connect(ui_->addRule, &QPushButton::clicked, this, [this] {
        if (rules_.size() < 32)
            addRule();
    });
    visualLabel_ = ui_->visualLabel;
    soundLabel_ = ui_->soundLabel;
    visualLabel_->setText(visual_.isEmpty() ? QStringLiteral("无图片")
                                            : QFileInfo(visual_).fileName());
    soundLabel_->setText(sound_.isEmpty() ? QStringLiteral("内置提示音")
                                          : QFileInfo(sound_).fileName());
    visualLabel_->setMaximumWidth(260);
    soundLabel_->setMaximumWidth(260);
    connect(ui_->chooseVisual, &QPushButton::clicked, this, [this] { chooseMedia(true); });
    connect(ui_->chooseSound, &QPushButton::clicked, this, [this] { chooseMedia(false); });
    connect(ui_->clearVisual, &QPushButton::clicked, this, [this] {
        visual_.clear();
        visualLabel_->setText(QStringLiteral("无图片"));
    });
    connect(ui_->clearSound, &QPushButton::clicked, this, [this] {
        sound_.clear();
        soundLabel_->setText(QStringLiteral("内置提示音"));
    });
    volume_ = ui_->volume;
    volume_->setValue(original_.volume);
    save_ = ui_->buttonBox->button(QDialogButtonBox::Save);
    save_->setText(QStringLiteral("保存提醒"));
    save_->setProperty("primary", true);
    save_->style()->unpolish(save_);
    save_->style()->polish(save_);
    ui_->buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui_->previewButton, &QPushButton::clicked, this,
            [this] { emit previewRequested(value()); });
    connect(save_, &QPushButton::clicked, this, [this] {
        const auto r = value();
        const auto error = ScheduleCalculator::validate(r);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("请检查输入"), error);
            return;
        }
        for (int i = 0; i < r.rules.size(); ++i) {
            for (int j = 0; j < i; ++j)
                if (ScheduleCalculator::sameSchedule(r.rules[i], r.rules[j])) {
                    QMessageBox::warning(this, QStringLiteral("重复时间"),
                                         QStringLiteral("请移除重复的时间规则。"));
                    return;
                }
            const auto &rule = r.rules[i];
            if (rule.kind == RuleKind::Once && rule.onceAt <= QDateTime::currentDateTimeUtc()) {
                bool unchanged = false;
                for (const auto &old : original_.rules)
                    if (old.id == rule.id && ScheduleCalculator::sameSchedule(old, rule))
                        unchanged = true;
                if (!unchanged) {
                    QMessageBox::warning(this, QStringLiteral("时间已过"),
                                         QStringLiteral("一次提醒请选择未来的时间。"));
                    return;
                }
            }
        }
        saving_ = true;
        save_->setEnabled(false);
        storage_->saveReminder(r);
    });
    connect(storage_, &StorageService::reminderSaved, this, [this](const Reminder &r) {
        if (saving_ && r.id == original_.id)
            accept();
    });
    connect(storage_, &StorageService::error, this, [this](const QString &message) {
        if (saving_) {
            saving_ = false;
            save_->setEnabled(true);
            QMessageBox::warning(this, QStringLiteral("保存失败"), message);
        }
    });
}
ReminderEditor::~ReminderEditor() = default;
Reminder ReminderEditor::value() const {
    auto r = original_;
    r.title = title_->text().trimmed();
    r.body = body_->toPlainText();
    r.enabled = enabled_->isChecked();
    r.volume = volume_->value();
    r.visual = visual_;
    r.sound = sound_;
    r.rules.clear();
    for (const auto *rule : rules_)
        r.rules.append(rule->value());
    return r;
}
void ReminderEditor::addRule(ScheduleRule rule) {
    auto *editor = new RuleEditor(std::move(rule), this);
    connect(editor->removeButton(), &QPushButton::clicked, this, [this, editor] {
        rules_.removeOne(editor);
        rulesLayout_->removeWidget(editor);
        editor->deleteLater();
    });
    rules_.append(editor);
    rulesLayout_->addWidget(editor);
}
void ReminderEditor::chooseMedia(bool image) {
    const auto source = QFileDialog::getOpenFileName(
        this, image ? QStringLiteral("选择图片或 GIF") : QStringLiteral("选择提醒声音"), QString{},
        image ? QStringLiteral("图片 (*.png *.jpg *.jpeg *.gif)")
              : QStringLiteral("音频 (*.wav *.mp3 *.ogg *.flac *.m4a)"));
    if (source.isEmpty())
        return;
    QString error;
    const auto path = media_->importFile(source, image, &error);
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), error);
        return;
    }
    if (image) {
        visual_ = path;
        visualLabel_->setText(QFileInfo(source).fileName());
    } else {
        sound_ = path;
        soundLabel_->setText(QFileInfo(source).fileName());
    }
}
} // namespace qding
