#include "ui/MainWindow.h"
#include "core/PackageCheck.h"
#include "BuildInformation.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("WorshipCue"));
    QApplication::setApplicationName(QStringLiteral("WorshipCue"));
    QApplication::setApplicationVersion(BuildInformation::version());
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/worshipcue.png")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (app.arguments().contains(QStringLiteral("--verify-package"))) {
        const int media = app.arguments().indexOf(QStringLiteral("--media-fixture"));
        return runPackageCheck(media >= 0 ? app.arguments().value(media + 1) : QString{});
    }
    MainWindow window;
    QString error;
    if (!window.initialize(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("WorshipCue could not start"),
            QStringLiteral("Unable to initialize the local library.\n\n%1\n\n"
                           "Ensure that the Qt SQLite plugin is installed.").arg(error));
        return 1;
    }
    window.show();
    return app.exec();
}
