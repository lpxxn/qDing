#pragma once
#include "domain/types.h"
#include <QDialog>
#include <memory>
namespace Ui {
class ReminderEditor;
}
class QLineEdit;
class QTextEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QVBoxLayout;
class QLabel;

namespace qding {
class MediaService;
class StorageService;
class RuleEditor;
class ReminderEditor final : public QDialog {
    Q_OBJECT
public:
    ReminderEditor(Reminder reminder, StorageService *storage, MediaService *media,
                   QWidget *parent = nullptr);
    ~ReminderEditor() override;
signals:
    void previewRequested(qding::Reminder reminder);

private:
    Reminder value() const;
    void addRule(ScheduleRule rule = {});
    void chooseMedia(bool image);
    std::unique_ptr<Ui::ReminderEditor> ui_;
    Reminder original_;
    StorageService *storage_;
    MediaService *media_;
    QList<RuleEditor *> rules_;
    QVBoxLayout *rulesLayout_;
    QLineEdit *title_;
    QTextEdit *body_;
    QSpinBox *volume_;
    QCheckBox *enabled_;
    QPushButton *save_;
    QLabel *visualLabel_;
    QLabel *soundLabel_;
    QString visual_, sound_;
    bool saving_ = false;
};
} // namespace qding
