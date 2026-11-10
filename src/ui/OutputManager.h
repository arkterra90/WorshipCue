#pragma once

#include "DisplayWindow.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVector>

class QScreen;
class QEvent;

struct OutputTarget {
    QString id;
    QString label;
};

// Owns routing and output-window lifetime. Desired display identities survive a
// disconnect; hidden outputs are recovered only when that display returns.
class OutputManager : public QObject {
    Q_OBJECT
public:
    explicit OutputManager(PresentationController *controller, QObject *parent = nullptr);
    ~OutputManager() override;
    DisplayWindow &audience() { return audience_; }
    DisplayWindow &stage() { return stage_; }
    const DisplayWindow &audience() const { return audience_; }
    const DisplayWindow &stage() const { return stage_; }
    QVector<OutputTarget> availableTargets() const;
    QString target(DisplayWindow::Kind kind) const;
    bool setTarget(DisplayWindow::Kind kind, const QString &id, QString *error = nullptr);
    QString status(DisplayWindow::Kind kind) const;
    void restoreTargets();

signals:
    void targetsChanged();
    void statusChanged();
    void operatorFocusRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    DisplayWindow audience_;
    DisplayWindow stage_;
    QString audienceTarget_ = QStringLiteral("off");
    QString stageTarget_ = QStringLiteral("off");
    QPointer<QScreen> audienceScreen_;
    QPointer<QScreen> stageScreen_;
    QSet<QScreen *> watchedScreens_;
    bool refreshPending_ = false;
    bool statusPending_ = false;

    DisplayWindow &window(DisplayWindow::Kind kind);
    const DisplayWindow &window(DisplayWindow::Kind kind) const;
    QString &desiredTarget(DisplayWindow::Kind kind);
    QPointer<QScreen> &activeScreen(DisplayWindow::Kind kind);
    static QString screenId(const QScreen *screen);
    static QScreen *findScreen(const QString &id);
    static QString settingsKey(DisplayWindow::Kind kind);
    static bool isDisplayTarget(const QString &id);
    void applyTarget(DisplayWindow::Kind kind);
    void persistTarget(DisplayWindow::Kind kind);
    void watchScreen(QScreen *screen);
    void scheduleRefresh();
    void scheduleStatusRefresh();
};
