#include "../src/core/LyricsImporter.h"
#include "../src/ui/SongEditorDialog.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class LyricsImportTest : public QObject {
    Q_OBJECT
private slots:
    void paragraphsAndSeparatorsProduceEditableSlides() {
        const auto imported = LyricsImporter::parse(
            QStringLiteral("\ufeffFirst line\r\nSecond line\r\n\r\nThird line\r\n---\r\nLast line\r\n"),
            QStringLiteral("A Song"));
        QCOMPARE(imported.title, QStringLiteral("A Song"));
        QCOMPARE(imported.slides, QStringList({QStringLiteral("First line\nSecond line"),
            QStringLiteral("Third line"), QStringLiteral("Last line")}));
    }

    void headingsSplitWithoutBeingProjected() {
        const auto imported = LyricsImporter::parse(QStringLiteral(
            "[Verse 1]\nFirst line\nSecond line\nChorus:\nSing together\n[Bridge]\nFinal line"));
        QCOMPARE(imported.slides, QStringList({QStringLiteral("First line\nSecond line"),
            QStringLiteral("Sing together"), QStringLiteral("Final line")}));
        QCOMPARE(imported.title, QStringLiteral("First line"));
    }

    void chordProMetadataAndChordsAreRemoved() {
        const auto imported = LyricsImporter::parse(QStringLiteral(
            "{title: Our Song}\n{artist: Example}\n{key: G}\n{comment: Verse 1}\n"
            "[G]Sing a [D/F#]new song\n[Em7]Lift your [Cadd9]voice\n"
            "{start_of_chorus}\n[C]We sing [G/B]together\n{end_of_chorus}"),
            QStringLiteral("filename"), true);
        QCOMPARE(imported.title, QStringLiteral("Our Song"));
        QCOMPARE(imported.slides, QStringList({QStringLiteral("Sing a new song\nLift your voice"),
            QStringLiteral("We sing together")}));
    }

    void clipboardChordsAreRecognizedWithoutRemovingOtherBrackets() {
        const auto imported = LyricsImporter::parse(QStringLiteral(
            "[Am]We sing [God] is good\n[Unknown instruction]\n[Fsus4]Always"));
        QCOMPARE(imported.slides, QStringList({QStringLiteral(
            "We sing [God] is good\n[Unknown instruction]\nAlways")}));
    }

    void emptyInputsAndRepeatedBreaksDoNotCreateEmptySlides() {
        QCOMPARE(LyricsImporter::parse(QStringLiteral("\n---\n[Chorus]\n\n")).slides.size(), 0);
        const auto imported = LyricsImporter::parse(QStringLiteral("---\nFirst\n---\n---\nSecond\n---"));
        QCOMPARE(imported.slides, QStringList({QStringLiteral("First"), QStringLiteral("Second")}));
    }

    void fileImportUsesFilenameAndUtf8AndKeepsEditsOnFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("Sunday Song.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray text = QStringLiteral("Grâce and peace\n\nSing again").toUtf8();
        QCOMPARE(file.write(text), qint64(text.size()));
        file.close();
        SongEditorDialog dialog;
        QString error;
        QVERIFY2(dialog.importLyricsFile(path, &error), qPrintable(error));
        QCOMPARE(dialog.title(), QStringLiteral("Sunday Song"));
        QCOMPARE(dialog.slides(), QStringList({QStringLiteral("Grâce and peace"), QStringLiteral("Sing again")}));
        QVERIFY(!dialog.importLyricsFile(directory.filePath(QStringLiteral("missing.txt")), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(dialog.slides().first(), QStringLiteral("Grâce and peace"));

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(QByteArray::fromHex("fffe4100")), qint64(4));
        file.close();
        QVERIFY(!dialog.importLyricsFile(path, &error));
        QVERIFY(error.contains(QStringLiteral("UTF-8")));
        QCOMPARE(dialog.title(), QStringLiteral("Sunday Song"));
        QCOMPARE(dialog.slides().first(), QStringLiteral("Grâce and peace"));
    }

    void chordProFileTitleAndPasteImportCanBeReviewedBeforeSave() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("source.cho")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("{title: Shared Song}\n[C]One line\n\n[G]Another") > 0);
        file.close();
        SongEditorDialog dialog;
        QString error;
        QVERIFY2(dialog.importLyricsFile(file.fileName(), &error), qPrintable(error));
        QCOMPARE(dialog.title(), QStringLiteral("Shared Song"));
        QCOMPARE(dialog.slides(), QStringList({QStringLiteral("One line"), QStringLiteral("Another")}));
        dialog.importLyricsText(QStringLiteral("New verse\n\nNew chorus"));
        QCOMPARE(dialog.title(), QStringLiteral("Shared Song"));
        QCOMPARE(dialog.slides(), QStringList({QStringLiteral("New verse"), QStringLiteral("New chorus")}));
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }
};

QTEST_MAIN(LyricsImportTest)
#include "lyrics_import_test.moc"
