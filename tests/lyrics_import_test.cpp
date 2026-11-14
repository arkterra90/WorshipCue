#include "../src/core/LyricsImporter.h"
#include "../src/ui/SongEditorDialog.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class LyricsImportTest : public QObject {
    Q_OBJECT
private slots:
    void repeatedNamedSectionsShareLyricsAndPreserveTheirSlideGroups() {
        const auto imported = LyricsImporter::parse(QStringLiteral(
            "[Verse 1]\nFirst verse\n[Chorus]\nFirst chorus page\n\nSecond chorus page\n"
            "[Verse 2]\nSecond verse\n[Chorus]\nFirst chorus page\n\nSecond chorus page"));
        QCOMPARE(imported.slides.size(), 6);
        QStringList sources;
        const auto structure = imported.songStructure(&sources);
        QCOMPARE(sources.size(), 4);
        QCOMPARE(structure.sections.size(), 3);
        QCOMPARE(structure.sections.at(1).name, QStringLiteral("Chorus"));
        QCOMPARE(structure.sections.at(1).slideCount, 2);
        const auto ids = structure.arrangements.first().sectionIds;
        QCOMPARE(ids.size(), 4);
        QCOMPARE(ids.at(1), ids.at(3));
        QCOMPARE(structure.slideOrder(), QVector<int>({0, 1, 2, 3, 1, 2}));
        QString error;
        QVERIFY2(structure.validate(int(sources.size()), &error), qPrintable(error));
        SongEditorDialog editor;
        editor.importLyricsText(QStringLiteral("[Chorus]\nSing\n[Chorus]\nSing"));
        const auto draft = editor.songStructure(&sources);
        QCOMPARE(sources, QStringList({QStringLiteral("Sing")}));
        QCOMPARE(draft.arrangements.first().sectionIds.size(), 2);
    }

    void differentlyWordedSectionsWithTheSameHeadingKeepBothVersions() {
        const auto imported = LyricsImporter::parse(QStringLiteral(
            "[Chorus]\nVersion A\n[Chorus]\nVersion B\n[Chorus]\nVersion A\n[Chorus]\nVersion B"));
        QStringList sources;
        const auto structure = imported.songStructure(&sources);
        QCOMPARE(sources, QStringList({QStringLiteral("Version A"), QStringLiteral("Version B")}));
        QCOMPARE(structure.sections.size(), 2);
        QVERIFY(structure.sections.first().name != structure.sections.last().name);
        QCOMPARE(structure.slideOrder(), QVector<int>({0, 1, 0, 1}));
    }

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
