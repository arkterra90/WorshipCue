#include "PackageCheck.h"
#include "../data/LibraryRepository.h"
#include "../licensing/LicenseManager.h"
#include "../ui/SlideRenderer.h"
#include "BuildInformation.h"
#include "BuiltInBackgrounds.h"
#include <QApplication>
#include <QAudioFormat>
#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QMediaPlayer>
#include <QPainter>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSslSocket>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
namespace {
QJsonObject failure(const QString &message) { return {{"ok", false}, {"error", message}}; }
QJsonObject inspectPackage(const QString &fixture) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return failure("Unable to create isolated package-check storage.");
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
        return failure("Missing SQLite plugin.");
    if (!QSslSocket::supportsSsl())
        return failure("No functioning TLS backend.");
    const auto configuration = LicenseConfiguration::production();
    if (configuration.endpoint.scheme() != "https" || configuration.publicKey.size() != 32)
        return failure("Missing public licensing configuration.");
    if (LicenseManager::validateLease("forged.authorization", configuration.publicKey, "fake",
                                      "fake", QDateTime::currentSecsSinceEpoch()))
        return failure("Invalid authorization was accepted.");
    QFile catalogue(":/help/topics.json");
    if (!catalogue.open(QIODevice::ReadOnly))
        return failure("Missing offline Help catalogue.");
    const auto topics = QJsonDocument::fromJson(catalogue.readAll()).array();
    if (topics.size() != 16)
        return failure("Offline Help catalogue is incomplete.");
    for (const auto &topic : topics) {
        QFile article(":/help/" + topic.toObject()["id"].toString() + ".md");
        if (!article.open(QIODevice::ReadOnly) || article.size() < 100)
            return failure("Missing offline Help article.");
    }
    QDir backgrounds(":/backgrounds/builtin");
    const auto images = backgrounds.entryList({"*.jpg"}, QDir::Files);
    if (images.size() != 20)
        return failure("Built-in background collection is incomplete.");
    for (const auto &name : images)
        if (QImage(backgrounds.filePath(name)).isNull())
            return failure("JPEG decoding plugin is unavailable.");
    if (QImage(":/icons/worshipcue.png").isNull())
        return failure("Application icon is missing.");
    LibraryRepository library;
    QString error;
    if (!library.open(&error, temporary.filePath("package library.sqlite3")))
        return failure(error);
    const int id = library.createTextPresentation("Package check",
                                                  {"Editor and library verification"}, &error);
    if (!id || library.song(id).slides.size() != 1)
        return failure("Packaged library migrations or persistence failed.");
    LivePresentation state;
    state.title = "Package check";
    state.slides = {"Rendering works"};
    state.index = 0;
    QImage canvas(640, 360, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    {
        QPainter painter(&canvas);
        SlideRenderer::paintAudience(painter, canvas.rect(), state, {});
    }
    if (!canvas.save(temporary.filePath("render.png")))
        return failure("PNG encoding failed.");
    bool videoDecoded = false;
    if (!fixture.isEmpty()) {
        QMediaPlayer player;
        QVideoSink sink;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                         [&](const QVideoFrame &frame) {
                             if (frame.isValid() && !frame.toImage().isNull()) {
                                 videoDecoded = true;
                                 loop.quit();
                             }
                         });
        QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, [&] { loop.quit(); });
        player.setVideoSink(&sink);
        player.setSource(QUrl::fromLocalFile(fixture));
        timeout.start(15000);
        player.play();
        loop.exec();
        player.stop();
        if (!videoDecoded)
            return failure("Packaged multimedia backend could not decode the video fixture.");
    }
    return {{"ok", true},
            {"version", BuildInformation::version()},
            {"channel", BuildInformation::channel()},
            {"qt", QString::fromLatin1(qVersion())},
            {"architecture", QSysInfo::buildCpuArchitecture()},
            {"helpTopics", topics.size()},
            {"backgrounds", images.size()},
            {"tls", QSslSocket::activeBackend()},
            {"videoDecoded", videoDecoded},
            {"pluginPaths", QJsonArray::fromStringList(QCoreApplication::libraryPaths())},
            {"qtLibrariesPath", QLibraryInfo::path(QLibraryInfo::LibrariesPath)},
            {"licenseConfigured", true},
            {"libraryMigrations", true}};
}
} // namespace
int runPackageCheck(const QString &fixture) {
    const auto result = inspectPackage(fixture);
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    return result["ok"].toBool() ? 0 : 1;
}
