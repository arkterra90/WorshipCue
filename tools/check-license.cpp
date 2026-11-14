// Explicit, opt-in native integration check. Reads credentials from a protected
// local file and never prints them. Uses the same production authority and OS
// activation store as the GUI. Not bundled or registered as an automated test.
#include "../src/licensing/LicenseManager.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("WorshipCue");
    app.setApplicationName("WorshipCue");
    const auto args = app.arguments();
    if (args.size() < 2 ||
        (args[1] != "--verify" && args[1] != "--activate-file" && args[1] != "--deactivate" &&
         args[1] != "--list-devices" && args[1] != "--release-device"))
        return 2;
    if (args[1] == "--release-device" && args.size() != 3)
        return 2;
    QString code;
    if (args[1] == "--activate-file") {
        if (args.size() != 3)
            return 2;
        QFile file(args[2]);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8192)
            return 2;
        code = QJsonDocument::fromJson(file.readAll()).object()["code"].toString();
        if (code.isEmpty())
            return 2;
    }
    LicenseManager manager;
    manager.start();
    bool waiting = false;
    QObject::connect(
        &manager, &LicenseManager::changed, &app, [&] {
            if (!waiting || manager.busy())
                return;
            if (args[1] == "--list-devices") {
                QTextStream(stdout)
                    << QJsonDocument(QJsonObject{{"currentInstallation", manager.installationId()},
                                                 {"devices", manager.devices()}})
                           .toJson(QJsonDocument::Compact)
                    << '\n';
            } else
                QTextStream(stdout) << manager.status() << '\n';
            app.exit(args[1] == "--deactivate" ? (manager.licenseId().isEmpty() ? 0 : 1)
                                               : (manager.liveAllowed() ? 0 : 1));
        });
    // Startup itself schedules verification if cached state exists. Queue this
    // operation after it, retrying once it completes so no request is dropped.
    QTimer retry;
    QObject::connect(&retry, &QTimer::timeout, &app, [&] {
        if (manager.busy())
            return;
        retry.stop();
        waiting = true;
        if (args[1] == "--activate-file")
            manager.activate(code, "WorshipCue development workstation");
        else if (args[1] == "--release-device")
            manager.releaseDevice(args[2]);
        else if (args[1] == "--deactivate")
            manager.deactivate();
        else
            manager.verifyOnline();
    });
    retry.start(100);
    QTimer::singleShot(35000, &app, [&] {
        QTextStream(stderr) << "Licensing check timed out; no credentials logged.\n";
        app.exit(1);
    });
    return app.exec();
}
