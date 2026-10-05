#include "platform/integration.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDir>
#include <QWidget>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wtsapi32.h>

namespace qding {
class WindowsIntegration : public QAbstractNativeEventFilter {
public:
    explicit WindowsIntegration(PlatformIntegration *owner) : owner_(owner) {
        window_ = new QWidget(nullptr, Qt::Tool);
        window_->setAttribute(Qt::WA_DontShowOnScreen);
        hwnd_ = reinterpret_cast<HWND>(window_->winId());
        WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
        qApp->installNativeEventFilter(this);
    }
    ~WindowsIntegration() override {
        qApp->removeNativeEventFilter(this);
        WTSUnRegisterSessionNotification(hwnd_);
        delete window_;
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
        const auto *msg = static_cast<MSG *>(message);
        if (msg->hwnd != hwnd_)
            return false;
        if (msg->message == WM_POWERBROADCAST) {
            if (msg->wParam == PBT_APMSUSPEND)
                emit owner_->aboutToSleep();
            else if (msg->wParam == PBT_APMRESUMEAUTOMATIC)
                emit owner_->resumed();
        } else if (msg->message == WM_WTSSESSION_CHANGE) {
            if (msg->wParam == WTS_SESSION_LOCK)
                emit owner_->lockedChanged(true);
            else if (msg->wParam == WTS_SESSION_UNLOCK)
                emit owner_->lockedChanged(false);
        }
        return false;
    }

private:
    PlatformIntegration *owner_;
    QWidget *window_;
    HWND hwnd_;
};
void *createNativeIntegration(PlatformIntegration *owner) {
    return new WindowsIntegration(owner);
}
void destroyNativeIntegration(void *native) {
    delete static_cast<WindowsIntegration *>(native);
}
constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool nativeLoginEnabled() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, runKey, L"qDing", RRF_RT_REG_SZ, nullptr, nullptr,
                        &size) == ERROR_SUCCESS;
}
bool setNativeLoginEnabled(bool enabled, QString *error) {
    HKEY key = nullptr;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                  &key, nullptr);
    if (result == ERROR_SUCCESS) {
        if (enabled) {
            const auto command =
                (QStringLiteral("\"") +
                 QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                 QStringLiteral("\" --background"))
                    .toStdWString();
            result = RegSetValueExW(key, L"qDing", 0, REG_SZ,
                                    reinterpret_cast<const BYTE *>(command.c_str()),
                                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        } else {
            result = RegDeleteValueW(key, L"qDing");
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS && error)
        *error = QStringLiteral("无法更新登录启动项，错误码 %1。").arg(result);
    return result == ERROR_SUCCESS;
}
} // namespace qding
