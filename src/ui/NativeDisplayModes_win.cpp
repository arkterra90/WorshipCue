#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "NativeDisplayModes.h"
#include <QScreen>
#include <QGuiApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QtGui/qscreen_platform.h>
#endif
#include <QSet>
#include <windows.h>
namespace {
QString deviceName(QScreen *screen) {
    if (!screen) return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (!native || !GetMonitorInfoW(native->handle(), reinterpret_cast<MONITORINFO *>(&info))) return {};
    return QString::fromWCharArray(info.szDevice);
#else
    if (QGuiApplication::platformName() != QStringLiteral("windows")) return {};
    QString matched;
    for (DWORD index = 0; ; ++index) {
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
        const auto name = QString::fromWCharArray(device.DeviceName);
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) || name != screen->name()) continue;
        if (!matched.isEmpty()) return {};
        matched = name;
    }
    return matched;
#endif
}
DisplayMode describe(const DEVMODEW &mode) {
    const auto id = QStringLiteral("%1x%2:%3:%4:%5").arg(mode.dmPelsWidth).arg(mode.dmPelsHeight)
        .arg(mode.dmDisplayFrequency).arg(mode.dmDisplayFlags).arg(mode.dmBitsPerPel);
    return {id, QSize(int(mode.dmPelsWidth), int(mode.dmPelsHeight)), QSize(int(mode.dmPelsWidth), int(mode.dmPelsHeight)),
        mode.dmDisplayFrequency > 1 ? double(mode.dmDisplayFrequency) : 0, (mode.dmDisplayFlags & DM_INTERLACED) ? 1 : 0};
}
class WindowsDisplayModes final : public DisplayModeProvider {
public:
    DisplayCapabilities capabilities(QScreen *screen) const override {
        DisplayCapabilities result;
        const auto name = deviceName(screen);
        if (name.isEmpty()) { result.explanation = "Native Windows display modes are unavailable for this screen/backend."; return result; }
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()), ENUM_CURRENT_SETTINGS, &mode)) result.currentMode = describe(mode).id;
        QSet<QString> included;
        for (DWORD index = 0; ; ++index) {
            mode = {}; mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()), index, &mode)) break;
            if (mode.dmBitsPerPel < 32) continue;
            const auto entry = describe(mode);
            if (!included.contains(entry.id)) { result.modes.append(entry); included.insert(entry.id); }
        }
        result.canSetMode = !result.modes.isEmpty() && included.contains(result.currentMode);
        result.explanation = "Windows reports these modes through EnumDisplaySettings. Changes use temporary fullscreen settings, without changing registry defaults.";
        if (!result.canSetMode) result.explanation += " The current desktop mode is not enumerated, so mode switching is unavailable.";
        return result;
    }
    bool apply(QScreen *screen, const QString &requested, QString *error) override {
        const auto name = deviceName(screen);
        for (DWORD index = 0; !name.isEmpty(); ++index) {
            DEVMODEW mode{}; mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()), index, &mode)) break;
            if (describe(mode).id != requested || mode.dmBitsPerPel < 32) continue;
            const auto device = reinterpret_cast<LPCWSTR>(name.utf16());
            mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS;
            if (mode.dmDisplayFrequency > 1) mode.dmFields |= DM_DISPLAYFREQUENCY;
            LONG status = ChangeDisplaySettingsExW(device, &mode, nullptr, CDS_TEST, nullptr);
            if (status == DISP_CHANGE_SUCCESSFUL) status = ChangeDisplaySettingsExW(device, &mode, nullptr, CDS_FULLSCREEN, nullptr);
            if (status == DISP_CHANGE_SUCCESSFUL) return true;
            if (error) *error = QStringLiteral("Windows refused the display mode (error %1).").arg(status);
            return false;
        }
        if (error) *error = "The requested native display mode is no longer available.";
        return false;
    }
};
}
std::shared_ptr<DisplayModeProvider> nativeDisplayModeProvider() { return std::make_shared<WindowsDisplayModes>(); }
