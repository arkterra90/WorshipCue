#pragma once
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>
class QScreen;
struct DisplayMode {
    QString id;
    QSize pixels;
    QSize desktop;
    double refreshHz = 0;
    // -1: OS did not expose scan information, 0: progressive, 1: interlaced.
    int interlaced = -1;
    QString label() const;
};
struct DisplayCapabilities {
    QVector<DisplayMode> modes;
    QString currentMode;
    bool canSetMode = false;
    QString explanation;
};
class DisplayModeProvider {
public:
    virtual ~DisplayModeProvider() = default;
    virtual DisplayCapabilities capabilities(QScreen *screen) const = 0;
    virtual bool apply(QScreen *screen, const QString &mode, QString *error) = 0;
};
std::shared_ptr<DisplayModeProvider> nativeDisplayModeProvider();
