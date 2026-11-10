#include "ui/MainWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("WorshipCue"));
    QApplication::setApplicationName(QStringLiteral("WorshipCue"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

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
