#include "platform/integration.h"
#import <AppKit/AppKit.h>
#include <QMetaObject>
#import <ServiceManagement/ServiceManagement.h>

namespace qding {
struct MacIntegration {
    id sleepObserver;
    id wakeObserver;
    id lockObserver;
    id unlockObserver;
};
void *createNativeIntegration(PlatformIntegration *owner) {
    auto *state = new MacIntegration;
    auto *center = NSWorkspace.sharedWorkspace.notificationCenter;
    state->sleepObserver =
        [center addObserverForName:NSWorkspaceWillSleepNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *) {
                          QMetaObject::invokeMethod(
                              owner, [owner] { emit owner->aboutToSleep(); }, Qt::QueuedConnection);
                        }];
    state->wakeObserver =
        [center addObserverForName:NSWorkspaceDidWakeNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *) {
                          QMetaObject::invokeMethod(
                              owner, [owner] { emit owner->resumed(); }, Qt::QueuedConnection);
                        }];
    auto *distributed = NSDistributedNotificationCenter.defaultCenter;
    state->lockObserver = [distributed
        addObserverForName:@"com.apple.screenIsLocked"
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification *) {
                  QMetaObject::invokeMethod(
                      owner, [owner] { emit owner->lockedChanged(true); }, Qt::QueuedConnection);
                }];
    state->unlockObserver = [distributed
        addObserverForName:@"com.apple.screenIsUnlocked"
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification *) {
                  QMetaObject::invokeMethod(
                      owner, [owner] { emit owner->lockedChanged(false); }, Qt::QueuedConnection);
                }];
    return state;
}
void destroyNativeIntegration(void *native) {
    auto *state = static_cast<MacIntegration *>(native);
    if (!state)
        return;
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:state->sleepObserver];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:state->wakeObserver];
    [NSDistributedNotificationCenter.defaultCenter removeObserver:state->lockObserver];
    [NSDistributedNotificationCenter.defaultCenter removeObserver:state->unlockObserver];
    delete state;
}
bool nativeLoginEnabled() {
    if (@available(macOS 13.0, *))
        return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
    return false;
}
bool setNativeLoginEnabled(bool enabled, QString *error) {
    if (@available(macOS 13.0, *)) {
        NSError *failure = nil;
        const BOOL result = enabled
                                ? [SMAppService.mainAppService registerAndReturnError:&failure]
                                : [SMAppService.mainAppService unregisterAndReturnError:&failure];
        if (!result && error)
            *error = QString::fromUtf8(failure.localizedDescription.UTF8String);
        return result;
    }
    if (error)
        *error = QStringLiteral("登录启动需要 macOS 13 或更新版本。");
    return false;
}
} // namespace qding
