#include "fixtures/SchemaDowngrade.h"
#include "../src/data/LibraryRepository.h"
#include "../src/ui/PresentationLookDialog.h"
#include "../src/ui/RichTextSupport.h"
#include "../src/ui/SlideRenderer.h"
#include "../src/core/BuiltInBackgrounds.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFontComboBox>
#include <QFontInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>
#include <QtTest>

namespace {
QString lyrics() { return QStringLiteral("First original line\nSecond original line\nThird original line\nFourth original line\nFifth original line"); }
PresentationLook largeLook() {
    PresentationLook look;
    look.fontSize = 192;
    look.marginTop = 400;
    look.marginBottom = 440;
    return look;
}
QVariant sql(const QString &path, const QString &statement) {
    const auto name = QUuid::createUuid().toString();
    QVariant result;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(statement)) result = query.next() ? query.value(0) : QVariant(true);
        }
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
}

class PresentationLookTest : public QObject {
    Q_OBJECT
    template<class T> T *control(QObject &parent, const char *name) {
        auto *widget = parent.findChild<T *>(QString::fromLatin1(name));
        Q_ASSERT(widget);
        return widget;
    }
private slots:
    void migratesVersionSevenAndRollsBackPartialLookMigration() {
        QTemporaryDir directory;
        const QStringList stripLookSchema{"DROP TABLE midi_bindings", "DROP TABLE midi_slide_actions",
            "ALTER TABLE songs DROP COLUMN look_id", "ALTER TABLE slides DROP COLUMN look_id",
            "ALTER TABLE slides DROP COLUMN look_rich_override", "ALTER TABLE slides DROP COLUMN look_fit",
            "DROP TABLE looks", "ALTER TABLE service_entries DROP COLUMN media_transition_type",
            "ALTER TABLE service_entries DROP COLUMN media_transition_duration_ms", "ALTER TABLE service_entries DROP COLUMN media_transition_override", "PRAGMA user_version=7"};
        for (bool conflictingColumn : {false, true}) {
            const auto path = directory.filePath(conflictingColumn ? QStringLiteral("bad.sqlite3") : QStringLiteral("old.sqlite3"));
            QString error;
            {
                LibraryRepository library;
                QVERIFY(library.open(&error, path));
            }
            QVERIFY(removeCleanupSchema(path));
            for (const auto &statement : stripLookSchema) QVERIFY2(sql(path, statement).isValid(), qPrintable(statement));
            if (conflictingColumn) QVERIFY(sql(path, QStringLiteral("ALTER TABLE slides ADD COLUMN look_fit INTEGER NOT NULL DEFAULT 0")).isValid());
            LibraryRepository migrated;
            if (conflictingColumn) {
                QVERIFY(!migrated.open(&error, path));
                QCOMPARE(sql(path, QStringLiteral("PRAGMA user_version")).toInt(), 7);
                QCOMPARE(sql(path, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='looks'")).toInt(), 0);
                QCOMPARE(sql(path, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('songs') WHERE name='look_id'")).toInt(), 0);
                QCOMPARE(sql(path, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('slides') WHERE name='look_rich_override'")).toInt(), 0);
            } else {
                QVERIFY2(migrated.open(&error, path), qPrintable(error));
                QCOMPARE(sql(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
                QCOMPARE(migrated.entrySlides(1).size(), 2);
                QCOMPARE(migrated.service(1).first().id, 1);
                QCOMPARE(migrated.song(1).looks.first().id, 0);
                QVERIFY(migrated.looks().isEmpty());
            }
        }
    }

    void lookSerializationAndValidation() {
        PresentationLook original;
        original.fontFamily = QStringLiteral("Helvetica");
        original.fontSize = 96; original.fontWeight = 400; original.italic = true;
        original.textColor = QStringLiteral("#ffeedd"); original.horizontalAlignment = 0; original.verticalAlignment = 2;
        original.marginLeft = 75; original.padding = 20;
        original.background = PresentationLook::Background::Image;
        original.backgroundPath = BuiltInBackgrounds::catalog().first().path;
        QString error;
        QVERIFY(original.validate(&error));
        PresentationLook restored;
        QVERIFY(PresentationLook::fromJson(original.toJson(), &restored, &error));
        QCOMPARE(restored.toJson(), original.toJson());
        auto bad = original;
        bad.marginRight = 1920;
        QVERIFY(!bad.validate(&error));
        bad = original; bad.backgroundPath = QStringLiteral("backgrounds/../secret.png");
        QVERIFY(!bad.validate(&error));
        bad = original; bad.fontSize = 0;
        QVERIFY(!bad.validate(&error));
        QVERIFY(!PresentationLook::fromJson(QStringLiteral("{}"), &restored, &error));
    }

    void lookApplicationPreservesContentAndSupportsRichOverrides() {
        const QString plain = QStringLiteral("One line\nAnother line");
        const QString html = QStringLiteral("<p><span style='font-size:24px;color:red'>One line</span></p><p>Another line</p>");
        SlideLook style{1, {}, false, false, {}};
        style.settings.fontSize = 88; style.settings.fontWeight = 400;
        style.settings.textColor = QStringLiteral("#00ff00"); style.settings.horizontalAlignment = 2;
        QTextDocument document;
        RichTextSupport::load(&document, plain, html, style);
        QCOMPARE(document.toPlainText(), plain);
        QTextCursor cursor(&document); cursor.setPosition(1);
        QCOMPARE(cursor.charFormat().font().pixelSize(), 88);
        QCOMPARE(cursor.charFormat().foreground().color(), QColor(Qt::green));
        QVERIFY(document.begin().blockFormat().alignment().testFlag(Qt::AlignRight));
        QTextDocument empty;
        RichTextSupport::load(&empty, {}, {}, style);
        QTextCursor typing(&empty);
        typing.insertText(QStringLiteral("New text"));
        QCOMPARE(typing.charFormat().font().pixelSize(), 88);
        QCOMPARE(typing.charFormat().foreground().color(), QColor(Qt::green));
        style.richTextOverrides = true;
        RichTextSupport::load(&document, plain, html, style);
        cursor = QTextCursor(&document); cursor.setPosition(1);
        QCOMPARE(cursor.charFormat().font().pixelSize(), 24);
        QCOMPARE(cursor.charFormat().foreground().color(), QColor(Qt::red));
        const auto captured = RichTextSupport::captureLook(plain, html);
        QCOMPARE(captured.fontSize, 24);
        QCOMPARE(QColor(captured.textColor), QColor(Qt::red));
        const auto afterBlank = RichTextSupport::captureLook(QStringLiteral("\nVisible"),
            QStringLiteral("<p></p><p><span style='font-size:40px'>Visible</span></p>"));
        QCOMPARE(afterBlank.fontSize, 40);
    }

    void measuredOverflowSplitsWithoutLosingTextOrBreakingUnicode() {
        const SlideLook style{1, largeLook(), false, false, {}};
        QVERIFY(RichTextSupport::overflows(lyrics(), {}, style));
        const auto split = RichTextSupport::split(lyrics(), {}, style);
        QVERIFY2(split.error.isEmpty(), qPrintable(split.error));
        QVERIFY(split.pages.size() > 1);
        QString reconstructed;
        for (const auto &page : split.pages) {
            reconstructed += page.text;
            QVERIFY2(!RichTextSupport::overflows(page.text, page.html, style), qPrintable(page.text));
            QTextDocument doc;
            RichTextSupport::load(&doc, page.text, page.html);
            QCOMPARE(doc.toPlainText(), page.text);
        }
        QCOMPARE(reconstructed, lyrics());
        auto narrow = style;
        narrow.settings.fontSize = 80; narrow.settings.marginLeft = 850; narrow.settings.marginRight = 850;
        narrow.settings.marginTop = 300; narrow.settings.marginBottom = 300;
        const QString unicode = QString::fromUtf8("Worship 🙌🏽 Grâce 🎵 peace ").repeated(16);
        const auto unicodeSplit = RichTextSupport::split(unicode, {}, narrow);
        QVERIFY2(unicodeSplit.error.isEmpty(), qPrintable(unicodeSplit.error));
        reconstructed.clear();
        for (const auto &page : unicodeSplit.pages) {
            reconstructed += page.text;
            QVERIFY(!page.text.front().isLowSurrogate());
            QVERIFY(!page.text.back().isHighSurrogate());
        }
        QCOMPARE(reconstructed, unicode);
        auto impossible = style;
        impossible.settings.marginTop = 530; impossible.settings.marginBottom = 530;
        QVERIFY(!RichTextSupport::split(QStringLiteral("One line"), {}, impossible).error.isEmpty());
    }

    void selectedAndPresentationApplicationPersistAndUndo() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        int lookId = 0;
        {
            LibraryRepository library;
            QVERIFY(library.open(&error, path));
            PresentationLook look;
            look.fontSize = 72; look.background = PresentationLook::Background::Color; look.backgroundColor = QStringLiteral("#123456");
            lookId = library.createLook(QStringLiteral("Sunday"), look, &error);
            QVERIFY(lookId > 0);
            QVERIFY(!library.createLook(QStringLiteral("sunday"), look, &error));
            const Song original = library.song(1);
            PresentationLookDialog dialog(&library, original);
            QVERIFY(dialog.applyLook(lookId, {1}, false, PresentationLookDialog::OverflowHandling::Cancel, &error));
            QCOMPARE(dialog.draft().appearance.lookId, 0);
            QCOMPARE(dialog.draft().appearance.slideLookIds, QVector<int>({0, lookId}));
            QCOMPARE(dialog.draft().slides, original.slides);
            control<QPushButton>(dialog, "undoLookChange")->click();
            QCOMPARE(dialog.draft().appearance, original.appearance);
            control<QPushButton>(dialog, "redoLookChange")->click();
            QCOMPARE(dialog.draft().appearance.slideLookIds.at(1), lookId);
            QVERIFY(dialog.applyLook(lookId, {}, true, PresentationLookDialog::OverflowHandling::Cancel, &error));
            QCOMPARE(dialog.draft().appearance.lookId, lookId);
            QCOMPARE(dialog.draft().appearance.slideLookIds, QVector<int>({0, 0}));
            QVERIFY2(library.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error), qPrintable(error));
            QCOMPARE(library.song(1).richText, original.richText);
            QCOMPARE(library.song(1).looks.first().settings.toJson(), look.toJson());
            const auto copy = library.song(1);
            const int copyId = library.createTextPresentation(QStringLiteral("Copy"), copy.slides, &error, copy.settings, copy.backgroundPath,
                copy.richText, copy.structure, copy.appearance);
            QVERIFY(copyId > 0);
            QCOMPARE(library.song(copyId).appearance, copy.appearance);
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.song(1).appearance.lookId, lookId);
        QCOMPARE(reopened.look(lookId).name, QStringLiteral("Sunday"));
        QCOMPARE(reopened.song(1).looks.first().settings.backgroundColor, QStringLiteral("#123456"));
    }

    void reflowPreservesSectionsPartialOrdersIdsAndUnrelatedSettings_data() {
        QTest::addColumn<bool>("stableIds");
        QTest::newRow("legacy-keys") << false;
        QTest::newRow("stable-slide-ids") << true;
    }

    void reflowPreservesSectionsPartialOrdersIdsAndUnrelatedSettings() {
        QFETCH(bool, stableIds);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, path));
        SongStructure structure;
        structure.sections = {{"v", "Verse", 1}, {"c", "Chorus", 2}};
        if (stableIds) { structure.sections[0].slideIds = {"v1"}; structure.sections[1].slideIds = {"c1", "c2"}; }
        structure.arrangements = {{"full", "Full", {"v", "c", "c"}}}; structure.activeArrangementId = QStringLiteral("full");
        const int id = library.createTextPresentation(QStringLiteral("Reflow song"), {"Verse", lyrics(), "Chorus end"}, &error, {}, {}, {"", "", ""}, structure);
        QVERIFY(id > 0);
        QVERIFY(library.appendToService(1, id, &error));
        const int entry = library.service(1).last().id;
        QVERIFY(library.setEntrySlideOrder(entry, {1, 0, 1, 2}, &error));
        const Song original = library.song(id);
        const int oldId = sql(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=1").arg(id)).toInt();
        const int lookId = library.createLook(QStringLiteral("Large"), largeLook(), &error);
        PresentationLookDialog dialog(&library, original, 1);
        QVERIFY(!dialog.applyLook(lookId, {1}, false, PresentationLookDialog::OverflowHandling::Cancel, &error));
        QCOMPARE(dialog.draft().slides, original.slides);
        QVERIFY2(dialog.applyLook(lookId, {1}, false, PresentationLookDialog::OverflowHandling::Split, &error), qPrintable(error));
        if (stableIds) {
            auto larger = largeLook(); larger.fontSize += 4;
            const int largerId = library.createLook("Larger again", larger, &error);
            QVERIFY(largerId > 0);
            QVERIFY2(dialog.applyLook(largerId, {}, true, PresentationLookDialog::OverflowHandling::Split, &error), qPrintable(error));
            const auto keys = dialog.draft().structure.slideKeys();
            QCOMPARE(keys.first(), QStringLiteral("v/v1"));
            QCOMPARE(keys.at(1), QStringLiteral("c/c1"));
            QCOMPARE(keys.last(), QStringLiteral("c/c2"));
        }
        const int fragments = int(dialog.draft().slides.size()) - 2;
        QVERIFY(fragments > 1);
        QCOMPARE(dialog.draft().structure.sections.at(1).id, QStringLiteral("c"));
        QCOMPARE(dialog.draft().structure.sections.at(1).slideCount, fragments + 1);
        const PresentationSettings timing{6000, true, SlideTransitionType::Fade, 800, true};
        QVERIFY(library.updatePresentationSettings(id, timing, &error));
        QVERIFY2(library.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error), qPrintable(error));
        const auto saved = library.song(id);
        QCOMPARE(saved.settings, timing);
        QCOMPARE(sql(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=1").arg(id)).toInt(), oldId);
        QVector<int> expected;
        for (int i = 0; i < fragments; ++i) expected.append(1 + i);
        expected.append(0);
        for (int i = 0; i < fragments; ++i) expected.append(1 + i);
        expected.append(fragments + 1);
        QCOMPARE(library.service(1).last().slideOrder, expected);
        const auto arranged = library.entrySlides(entry);
        QString chorus;
        for (int i = 0; i < fragments; ++i) chorus += arranged.at(i);
        QCOMPARE(chorus, lyrics());
        QVERIFY(!library.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error));
        QVERIFY(error.contains(QStringLiteral("changed while editing")));
    }

    void failedReflowRollsBackAndFitIsAnExplicitOverride() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, path));
        const int id = library.createTextPresentation(QStringLiteral("Overflow"), {lyrics()}, &error);
        const int look = library.createLook(QStringLiteral("Large"), largeLook(), &error);
        const Song original = library.song(id);
        PresentationLookDialog dialog(&library, original);
        QVERIFY(dialog.applyLook(look, {}, true, PresentationLookDialog::OverflowHandling::Split, &error));
        QVERIFY(sql(path, QStringLiteral("CREATE TRIGGER reject_look BEFORE UPDATE OF look_id ON songs BEGIN SELECT RAISE(ABORT,'reject Look'); END")).toBool());
        QVERIFY(!library.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error));
        QVERIFY(error.contains(QStringLiteral("reject Look")));
        QCOMPARE(library.song(id).slides, original.slides);
        QCOMPARE(library.song(id).appearance, original.appearance);
        control<QPushButton>(dialog, "undoLookChange")->click();
        QVERIFY(dialog.applyLook(look, {}, true, PresentationLookDialog::OverflowHandling::Fit, &error));
        QCOMPARE(dialog.draft().slides, original.slides);
        QVERIFY(dialog.draft().appearance.fitText.first());
    }

    void mediaOverlaySplittingSharesFilesAndSupportsSafeRenderingAfterTextEdits() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        const SlideMedia media{MediaKind::Image, "media/example/still.png", "Still.png", false};
        const int id = library.createPresentation(QStringLiteral("Picture overlay"), {lyrics()}, {media}, &error);
        QVERIFY(id > 0);
        const int lookId = library.createLook(QStringLiteral("Large"), largeLook(), &error);
        const auto original = library.song(id);
        PresentationLookDialog dialog(&library, original);
        QVERIFY(dialog.applyLook(lookId, {}, true, PresentationLookDialog::OverflowHandling::Split, &error));
        QVERIFY(library.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error));
        const auto saved = library.song(id);
        QVERIFY(saved.media.size() > 1);
        for (const auto &asset : saved.media) QCOMPARE(asset, media);
        QCOMPARE(library.mediaAssets().size(), 1);
        LivePresentation state;
        state.index = 0; state.slides = {lyrics()}; state.looks = {{lookId, largeLook(), false, false, {}}};
        QImage automatic(1920, 1080, QImage::Format_ARGB32_Premultiplied), fitted = automatic;
        { QPainter painter(&automatic); SlideRenderer::paintAudience(painter, automatic.rect(), state); }
        state.looks[0].fitText = true;
        { QPainter painter(&fitted); SlideRenderer::paintAudience(painter, fitted.rect(), state); }
        QCOMPARE(automatic, fitted);
    }

    void uiWarnsReviewsSplitsAndCanCancelOrUndo() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        const int id = library.createTextPresentation(QStringLiteral("Review"), {lyrics()}, &error);
        const int lookId = library.createLook(QStringLiteral("Large"), largeLook(), &error);
        PresentationLookDialog dialog(&library, library.song(id));
        control<QComboBox>(dialog, "savedLooks")->setCurrentIndex(control<QComboBox>(dialog, "savedLooks")->findData(lookId));
        QTimer::singleShot(0, [] {
            auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            Q_ASSERT(warning && warning->objectName() == QStringLiteral("lookOverflowWarning"));
            warning->button(QMessageBox::Cancel)->click();
        });
        control<QPushButton>(dialog, "applyLookToPresentation")->click();
        QCOMPARE(dialog.draft().slides.size(), 1);
        auto *family = control<QFontComboBox>(dialog, "lookFontFamily");
        const auto system = RichTextSupport::defaultFont();
        if (family->findText(system.family(), Qt::MatchFixedString) < 0
            && family->findText(QFontInfo(system).family(), Qt::MatchFixedString) < 0)
            QCOMPARE(family->currentText(), QStringLiteral("System font"));
        QTimer::singleShot(0, [] {
            auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            Q_ASSERT(warning);
            QTimer::singleShot(0, [] {
                auto *review = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(review && review->objectName() == QStringLiteral("lookSplitPreview"));
                auto *list = review->findChild<QListWidget *>(QStringLiteral("splitPreviewSlides"));
                Q_ASSERT(list && list->count() > 1);
                review->findChild<QDialogButtonBox *>(QStringLiteral("splitPreviewActions"))->button(QDialogButtonBox::Apply)->click();
            });
            warning->findChild<QPushButton *>(QStringLiteral("previewLookSplit"))->click();
        });
        control<QPushButton>(dialog, "applyLookToPresentation")->click();
        QVERIFY(dialog.draft().slides.size() > 1);
        control<QPushButton>(dialog, "undoLookChange")->click();
        QCOMPARE(dialog.draft().slides, QStringList({lyrics()}));
        control<QPushButton>(dialog, "redoLookChange")->click();
        QVERIFY(dialog.draft().slides.size() > 1);
        QCOMPARE(library.song(id).slides, QStringList({lyrics()}));
        if (qEnvironmentVariableIsSet("WORSHIPCUE_LOOK_SCREENSHOT"))
            QVERIFY(dialog.grab().save(qEnvironmentVariable("WORSHIPCUE_LOOK_SCREENSHOT")));
    }

    void backgroundColorPositionAndPaddingReachRenderer() {
        SlideLook look{1, {}, false, false, {}};
        look.settings.background = PresentationLook::Background::Color;
        look.settings.backgroundColor = QStringLiteral("#123456");
        look.settings.fontSize = 60; look.settings.marginTop = 100; look.settings.padding = 40;
        look.settings.horizontalAlignment = 0; look.settings.verticalAlignment = 0;
        LivePresentation state;
        state.index = 0; state.slides = {"Visible words"}; state.looks = {look};
        QImage image(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        { QPainter painter(&image); SlideRenderer::paintAudience(painter, image.rect(), state); }
        QCOMPARE(image.pixelColor(20, 20), QColor(QStringLiteral("#123456")));
        int firstY = -1;
        for (int y = 0; y < image.height() && firstY < 0; ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).lightness() > 180) { firstY = y; break; }
        QVERIFY(firstY >= 140 && firstY < 240);
    }
};

QTEST_MAIN(PresentationLookTest)
#include "presentation_look_test.moc"
