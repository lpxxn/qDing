#pragma once
#include <QObject>

namespace qding {
// 所有系统差异集中在 platform；业务层只订阅语义事件。
class PlatformIntegration final : public QObject {
    Q_OBJECT
public:
    explicit PlatformIntegration(QObject *parent = nullptr);
    ~PlatformIntegration() override;
    bool loginEnabled() const;
    bool setLoginEnabled(bool enabled, QString *error);
signals:
    void aboutToSleep();
    void resumed();
    void lockedChanged(bool locked);

private:
    void *native_ = nullptr;
};
void *createNativeIntegration(PlatformIntegration *owner);
void destroyNativeIntegration(void *native);
bool nativeLoginEnabled();
bool setNativeLoginEnabled(bool enabled, QString *error);
} // namespace qding
