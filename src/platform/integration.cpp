#include "platform/integration.h"
namespace qding {
PlatformIntegration::PlatformIntegration(QObject *parent)
    : QObject(parent), native_(createNativeIntegration(this)) {}
PlatformIntegration::~PlatformIntegration() {
    destroyNativeIntegration(native_);
}
bool PlatformIntegration::loginEnabled() const {
    return nativeLoginEnabled();
}
bool PlatformIntegration::setLoginEnabled(bool enabled, QString *error) {
    return setNativeLoginEnabled(enabled, error);
}
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
void *createNativeIntegration(PlatformIntegration *) {
    return nullptr;
}
void destroyNativeIntegration(void *) {}
bool nativeLoginEnabled() {
    return false;
}
bool setNativeLoginEnabled(bool, QString *error) {
    if (error)
        *error = QStringLiteral("此平台暂未实现登录启动。");
    return false;
}
#endif
} // namespace qding
