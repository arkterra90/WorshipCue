#include "NativeDisplayModes.h"
#include <QScreen>
#include <QGuiApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
#include <QtGui/qscreen_platform.h>
#endif
#include <QSet>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <IOKit/graphics/IOGraphicsTypes.h>

namespace {
CGDirectDisplayID displayId(QScreen *screen) {
    if (!screen) return kCGNullDirectDisplay;
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    auto *native = screen->nativeInterface<QNativeInterface::QCocoaScreen>();
    if (!native || !native->nativeScreen()) return kCGNullDirectDisplay;
    return [[native->nativeScreen() deviceDescription][@"NSScreenNumber"] unsignedIntValue];
#else
    // Qt before 6.11 has no public Cocoa screen handle. Bind only an exact,
    // unique OS name/desktop-geometry match, never guess from screen order.
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return kCGNullDirectDisplay;
    const auto screens = [NSScreen screens];
    const CGFloat primaryTop = screens.count ? NSMaxY(screens.firstObject.frame) : 0;
    CGDirectDisplayID matched = kCGNullDirectDisplay;
    for (NSScreen *candidate in screens) {
        const NSRect frame = candidate.frame;
        const QRect geometry(qRound(frame.origin.x), qRound(primaryTop - NSMaxY(frame)), qRound(frame.size.width), qRound(frame.size.height));
        if (QString::fromNSString(candidate.localizedName) != screen->name() || geometry != screen->geometry()) continue;
        if (matched) return kCGNullDirectDisplay;
        matched = [candidate.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    }
    return matched;
#endif
}
CFArrayRef copyModes(CGDirectDisplayID display) {
    const void *keys[] = {kCGDisplayShowDuplicateLowResolutionModes};
    const void *values[] = {kCFBooleanTrue};
    auto options = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto modes = CGDisplayCopyAllDisplayModes(display, options);
    if (options) CFRelease(options);
    return modes;
}
DisplayMode describe(CGDisplayModeRef mode) {
    const auto key = QStringLiteral("%1:%2x%3:%4x%5:%6:%7")
        .arg(CGDisplayModeGetIODisplayModeID(mode)).arg(CGDisplayModeGetPixelWidth(mode)).arg(CGDisplayModeGetPixelHeight(mode))
        .arg(CGDisplayModeGetWidth(mode)).arg(CGDisplayModeGetHeight(mode)).arg(CGDisplayModeGetRefreshRate(mode), 0, 'f', 6)
        .arg((CGDisplayModeGetIOFlags(mode) & kDisplayModeInterlacedFlag) ? 1 : 0);
    return {key,
        QSize(int(CGDisplayModeGetPixelWidth(mode)), int(CGDisplayModeGetPixelHeight(mode))),
        QSize(int(CGDisplayModeGetWidth(mode)), int(CGDisplayModeGetHeight(mode))),
        CGDisplayModeGetRefreshRate(mode), (CGDisplayModeGetIOFlags(mode) & kDisplayModeInterlacedFlag) ? 1 : 0};
}
class MacDisplayModes final : public DisplayModeProvider {
public:
    DisplayCapabilities capabilities(QScreen *screen) const override {
        DisplayCapabilities result;
        const auto id = displayId(screen);
        if (!id) { result.explanation = "Native macOS display modes are unavailable for this screen/backend."; return result; }
        auto current = CGDisplayCopyDisplayMode(id);
        uint32_t currentNativeId = 0;
        DisplayMode currentDescription;
        if (current) { currentNativeId = CGDisplayModeGetIODisplayModeID(current); currentDescription = describe(current); result.currentMode = currentDescription.id; CGDisplayModeRelease(current); }
        auto modes = copyModes(id);
        QSet<QString> included;
        if (modes) {
            for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
                auto mode = static_cast<CGDisplayModeRef>(const_cast<void *>(CFArrayGetValueAtIndex(modes, i)));
                if (!CGDisplayModeIsUsableForDesktopGUI(mode)) continue;
                const auto entry = describe(mode);
                // Normalize the current key through the reported mode list;
                // drivers may round the refresh value differently on a copy.
                if (!result.currentMode.isEmpty() && CGDisplayModeGetIODisplayModeID(mode) == currentNativeId
                    && entry.pixels == currentDescription.pixels && entry.desktop == currentDescription.desktop
                    && entry.interlaced == currentDescription.interlaced && qAbs(entry.refreshHz - currentDescription.refreshHz) < .2)
                    result.currentMode = entry.id;
                if (!included.contains(entry.id)) { result.modes.append(entry); included.insert(entry.id); }
            }
            CFRelease(modes);
        }
        result.canSetMode = !result.modes.isEmpty() && included.contains(result.currentMode);
        result.explanation = "macOS reports these desktop modes through Core Graphics. HiDPI modes show backing pixels and logical desktop dimensions; the driver controls wire timing. Refresh may be unspecified for variable-rate displays. Mode changes are temporary and restored when the route closes.";
        if (!result.canSetMode) result.explanation += " The current desktop mode is not enumerated, so mode switching is unavailable.";
        return result;
    }
    bool apply(QScreen *screen, const QString &requested, QString *error) override {
        const auto id = displayId(screen);
        auto modes = id ? copyModes(id) : nullptr;
        CGError status = kCGErrorIllegalArgument;
        bool found = false;
        if (modes) {
            for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
                auto mode = static_cast<CGDisplayModeRef>(const_cast<void *>(CFArrayGetValueAtIndex(modes, i)));
                if (describe(mode).id != requested || !CGDisplayModeIsUsableForDesktopGUI(mode)) continue;
                found = true; status = CGDisplaySetDisplayMode(id, mode, nullptr); break;
            }
            CFRelease(modes);
        }
        if (status == kCGErrorSuccess) return true;
        if (error) *error = found ? QStringLiteral("macOS refused the display mode (Core Graphics error %1).").arg(int(status))
                                 : QStringLiteral("The requested native display mode is no longer available.");
        return false;
    }
};
}
std::shared_ptr<DisplayModeProvider> nativeDisplayModeProvider() { return std::make_shared<MacDisplayModes>(); }
