#include "NativeDisplayModes.h"
#include <QScreen>

QString DisplayMode::label() const {
    QString result = QStringLiteral("%1×%2").arg(pixels.width()).arg(pixels.height());
    if (desktop != pixels && !desktop.isEmpty()) result += QStringLiteral(" (desktop %1×%2)").arg(desktop.width()).arg(desktop.height());
    result += refreshHz > 1 ? QStringLiteral(" · %1 Hz").arg(refreshHz, 0, 'f', 2) : QStringLiteral(" · refresh not reported");
    return result + (interlaced < 0 ? QStringLiteral(" · scan not reported") : interlaced ? QStringLiteral(" · interlaced") : QStringLiteral(" · progressive"));
}
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
class DesktopDisplayModes final : public DisplayModeProvider {
public:
    DisplayCapabilities capabilities(QScreen *screen) const override {
        DisplayCapabilities result;
        result.explanation = QStringLiteral("This platform exposes desktop geometry through Qt. Change physical modes in the operating system; logical canvas settings remain available.");
        if (screen) {
            const auto pixels = screen->size() * screen->devicePixelRatio();
            result.modes.append({"desktop", pixels, screen->size(), screen->refreshRate(), -1}); result.currentMode = "desktop";
        }
        return result;
    }
    bool apply(QScreen *, const QString &, QString *error) override {
        if (error) *error = QStringLiteral("Physical mode switching is not available on this platform."); return false;
    }
};
std::shared_ptr<DisplayModeProvider> nativeDisplayModeProvider() { return std::make_shared<DesktopDisplayModes>(); }
#endif
