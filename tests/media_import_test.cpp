#include "../src/media/MediaManager.h"
#include "fixtures/AudioFixture.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class MediaImportTest : public QObject {
    Q_OBJECT
    static QString image(const QString &directory, const QString &name) {
        const QString path = QDir(directory).filePath(name);
        QImage pixels(800, 600, QImage::Format_RGB32);
        pixels.fill(Qt::darkGreen);
        return pixels.save(path) ? path : QString();
    }
    static MediaImportResult result(QSignalSpy &spy) {
        return qvariant_cast<MediaImportResult>(spy.takeFirst().at(0));
    }
private slots:
    void audioImportsAsAStandalonePlayableAsset() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString path = source.filePath(QStringLiteral("music.wav"));
        QVERIFY(writeAudioFixture(path));
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles({path}, {QStringLiteral("Prelude.wav")}, true));
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QCOMPARE(imported.media.size(), 1);
        QCOMPARE(imported.media.first().kind, MediaKind::Audio);
        QVERIFY(imported.media.first().loop);
        QCOMPARE(QFileInfo(manager.absolutePath(imported.media.first().path)).size(), QFileInfo(path).size());
    }
    void copiesReadableNamesWithoutChangingSources() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.jpg"));
        const QString two = image(source.path(), QStringLiteral("2.png"));
        QVERIFY(!one.isEmpty() && !two.isEmpty());
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QSignalSpy progress(&manager, &MediaManager::progressChanged);
        QVERIFY(manager.importFiles({one, two}, {QStringLiteral("Sunday 01.jpg"), QStringLiteral("Sunday 02.png")}));
        QVERIFY(manager.busy());
        QVERIFY(!manager.importFiles({one}, {QStringLiteral("Other.jpg")}));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!manager.busy());
        const auto imported = result(finished);
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QCOMPARE(imported.media.size(), 2);
        QCOMPARE(imported.media.first().kind, MediaKind::Image);
        QCOMPARE(imported.media.first().name, QStringLiteral("Sunday 01.jpg"));
        QVERIFY(!QDir::isAbsolutePath(imported.media.first().path));
        const QString managed = manager.absolutePath(imported.media.first().path);
        QVERIFY(QFileInfo::exists(managed));
        QCOMPARE(QFileInfo(managed).fileName(), QStringLiteral("Sunday 01.jpg"));
        QVERIFY(QFileInfo::exists(one));
        QFile original(one), copied(managed);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), copied.readAll());
        QTRY_COMPARE(progress.count(), 2);
        original.close(); copied.close();
        QVERIFY(manager.discardImport(imported.directory));
        QVERIFY(!QFileInfo::exists(managed));
        QVERIFY(QFileInfo::exists(one));
        QVERIFY(!manager.discardImport(source.path()));
    }
    void failureRollsBackEntireBatch() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.png"));
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles({one, source.filePath(QStringLiteral("missing.png"))}, {QStringLiteral("One.png"), QStringLiteral("Two.png")}));
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY(!imported.error.isEmpty());
        QVERIFY(imported.media.isEmpty());
        QVERIFY(!QFileInfo::exists(imported.directory));
        QVERIFY(QFileInfo::exists(one));
    }
    void videoImportPreservesManagedReferenceAndLoopChoice() {
        QTemporaryDir library;
        const QString source = QFINDTESTDATA("fixtures/media-colors.mov");
        QVERIFY(!source.isEmpty());
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles({source}, {QStringLiteral("Welcome 01.mov")}, true));
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QCOMPARE(imported.media.size(), 1);
        QCOMPARE(imported.media.first().kind, MediaKind::Video);
        QVERIFY(imported.media.first().loop);
        QCOMPARE(QFileInfo(manager.absolutePath(imported.media.first().path)).size(), QFileInfo(source).size());
        QVERIFY(QFileInfo::exists(source));
    }
    void cancellationLeavesNoBatchFiles() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.png"));
        QStringList files, names;
        for (int index = 0; index < 500; ++index) {
            files.append(one);
            names.append(QStringLiteral("Photo %1.png").arg(index));
        }
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles(files, names));
        manager.cancelImport();
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY(imported.canceled);
        QVERIFY(imported.media.isEmpty());
        if (!imported.directory.isEmpty()) QVERIFY(!QFileInfo::exists(imported.directory));
        QVERIFY(QFileInfo::exists(one));
    }
    void invalidNamesOrImagesDoNotCreateManagedFiles() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.png"));
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles({one}, {QStringLiteral("../outside.png")}));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!result(finished).error.isEmpty());
        const QString bad = source.filePath(QStringLiteral("broken.jpg"));
        QFile file(bad);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image"); file.close();
        QVERIFY(manager.importFiles({one, bad}, {QStringLiteral("One.png"), QStringLiteral("Bad.jpg")}));
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY(!imported.error.isEmpty());
        QVERIFY(!QFileInfo::exists(imported.directory));
    }
    void thumbnailsAreAsynchronousBoundedAndPathsStayInsideLibrary() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.png"));
        MediaManager manager(library.path());
        QSignalSpy finished(&manager, &MediaManager::importFinished);
        QVERIFY(manager.importFiles({one}, {QStringLiteral("Photo.png")}));
        QTRY_COMPARE(finished.count(), 1);
        const auto imported = result(finished);
        QVERIFY(imported.error.isEmpty());
        const QString path = imported.media.first().path;
        QSignalSpy ready(&manager, &MediaManager::thumbnailReady);
        QVERIFY(manager.thumbnail(path).isNull());
        QTRY_COMPARE(ready.count(), 1);
        const QPixmap thumbnail = manager.thumbnail(path);
        QVERIFY(!thumbnail.isNull());
        QVERIFY(thumbnail.width() <= 512 && thumbnail.height() <= 288);
        QCOMPARE(ready.first().at(0).toString(), path);
        QVERIFY(manager.absolutePath(QStringLiteral("media/../../outside.png")).isEmpty());
        QVERIFY(manager.absolutePath(one).isEmpty());
        QVERIFY(manager.absolutePath(QStringLiteral("media\\outside.png")).isEmpty());
        QVERIFY(!manager.discardImport(library.path()));
    }
    void closingManagerCleansUndeliveredCompletedImport() {
        QTemporaryDir source;
        QTemporaryDir library;
        const QString one = image(source.path(), QStringLiteral("1.png"));
        {
            MediaManager manager(library.path());
            QVERIFY(manager.importFiles({one}, {QStringLiteral("Photo.png")}));
            // Destruction cancels/waits without delivering a result to persistence.
        }
        const QDir media(library.filePath(QStringLiteral("media")));
        QCOMPARE(media.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
        QVERIFY(QFileInfo::exists(one));
    }
};

QTEST_MAIN(MediaImportTest)
#include "media_import_test.moc"
