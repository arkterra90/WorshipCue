#include "../src/ui/SlideEditorDialog.h"
#include "../src/ui/RichTextSupport.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontInfo>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QtTest>

class SlideEditorTest : public QObject {
    Q_OBJECT
    template<class T> T *control(SlideEditorDialog &dialog, const char *name) {
        auto *result = dialog.findChild<T *>(QString::fromLatin1(name));
        Q_ASSERT(result);
        return result;
    }
    QGraphicsTextItem *textItem(SlideEditorDialog &dialog) {
        auto *canvas = control<QGraphicsView>(dialog, "slideTextCanvas");
        for (auto *item : canvas->scene()->items())
            if (auto *text = dynamic_cast<QGraphicsTextItem *>(item)) return text;
        Q_ASSERT(false);
        return nullptr;
    }
    static Song example() {
        Song song;
        song.id = 41;
        song.title = QStringLiteral("A Song");
        song.slides = {QStringLiteral("First words"), QStringLiteral("Second slide")};
        return song;
    }
    void select(SlideEditorDialog &dialog, int first, int count) {
        auto *item = textItem(dialog);
        QTextCursor cursor(item->document());
        cursor.setPosition(first);
        cursor.setPosition(first + count, QTextCursor::KeepAnchor);
        item->setTextCursor(cursor);
    }
private slots:
    void selectedWordFormattingSurvivesNavigationAndSave() {
        const Song source = example();
        SlideEditorDialog dialog(source);
        auto *item = textItem(dialog);
        select(dialog, 0, 5);
        QTest::mouseClick(control<QToolButton>(dialog, "slideItalic"), Qt::LeftButton);
        QTextCursor formatted(item->document());
        formatted.setPosition(1);
        QVERIFY(formatted.charFormat().fontItalic());
        formatted.setPosition(8);
        QVERIFY(!formatted.charFormat().fontItalic());
        control<QListWidget>(dialog, "slideEditorSlides")->setCurrentRow(1);
        QCOMPARE(dialog.selectedSlideIndex(), 1);
        QCOMPARE(textItem(dialog)->document()->toPlainText(), QStringLiteral("Second slide"));
        control<QListWidget>(dialog, "slideEditorSlides")->setCurrentRow(0);
        formatted = QTextCursor(textItem(dialog)->document());
        formatted.setPosition(1);
        QVERIFY(formatted.charFormat().fontItalic());
        const auto html = dialog.richText();
        QTextDocument restored;
        RichTextSupport::load(&restored, dialog.slides().first(), html.first());
        formatted = QTextCursor(&restored);
        formatted.setPosition(1);
        QVERIFY(formatted.charFormat().fontItalic());
        QCOMPARE(restored.toPlainText(), source.slides.first());
        QCOMPARE(dialog.richText().at(1), QString());
        QCOMPARE(source.richText, QStringList());
        auto *actions = control<QDialogButtonBox>(dialog, "slideEditorActions");
        QTest::mouseClick(actions->button(QDialogButtonBox::Save), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void fullSlideFontAlignmentAndUndoRedoStayWithEachSlide() {
        SlideEditorDialog dialog(example());
        control<QSpinBox>(dialog, "slideEditorFontSize")->setValue(160);
        auto *item = textItem(dialog);
        QTextCursor cursor(item->document());
        cursor.setPosition(2);
        QCOMPARE(cursor.charFormat().intProperty(QTextFormat::FontPixelSize), 160);
        QTest::mouseClick(control<QToolButton>(dialog, "slideAlignRight"), Qt::LeftButton);
        QVERIFY(item->document()->begin().blockFormat().alignment().testFlag(Qt::AlignRight));
        control<QListWidget>(dialog, "slideEditorSlides")->setCurrentRow(1);
        control<QListWidget>(dialog, "slideEditorSlides")->setCurrentRow(0);
        QVERIFY(control<QToolButton>(dialog, "slideUndo")->isEnabled());
        QTest::mouseClick(control<QToolButton>(dialog, "slideUndo"), Qt::LeftButton);
        QVERIFY(item->document()->begin().blockFormat().alignment().testFlag(Qt::AlignHCenter));
        QTest::mouseClick(control<QToolButton>(dialog, "slideRedo"), Qt::LeftButton);
        QVERIFY(item->document()->begin().blockFormat().alignment().testFlag(Qt::AlignRight));
    }

    void editsAndCancelNeverMutateOriginalSong() {
        const Song source = example();
        SlideEditorDialog dialog(source, 1);
        auto *item = textItem(dialog);
        QTextCursor cursor(item->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(QStringLiteral("Changed draft"));
        control<QLineEdit>(dialog, "slideEditorTitle")->setText(QStringLiteral("Draft title"));
        QCOMPARE(dialog.slides().at(1), QStringLiteral("Changed draft"));
        auto *actions = control<QDialogButtonBox>(dialog, "slideEditorActions");
        QTest::mouseClick(actions->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(source.title, QStringLiteral("A Song"));
        QCOMPARE(source.slides.at(1), QStringLiteral("Second slide"));
    }

    void newPresentationCanAddDuplicateAndRemoveDraftSlides() {
        SlideEditorDialog dialog(Song{});
        QCOMPARE(dialog.slides().size(), 1);
        auto *item = textItem(dialog);
        QTextCursor cursor(item->document());
        cursor.insertText(QStringLiteral("Welcome"));
        QTest::mouseClick(control<QPushButton>(dialog, "slideEditorDuplicateSlide"), Qt::LeftButton);
        QCOMPARE(dialog.slides(), (QStringList{QStringLiteral("Welcome"), QStringLiteral("Welcome")}));
        QTest::mouseClick(control<QPushButton>(dialog, "slideEditorAddSlide"), Qt::LeftButton);
        QCOMPARE(dialog.slides().size(), 3);
        QVERIFY(dialog.slides().last().isEmpty());
        QTest::mouseClick(control<QPushButton>(dialog, "slideEditorRemoveSlide"), Qt::LeftButton);
        QCOMPARE(dialog.slides().size(), 2);
        control<QLineEdit>(dialog, "slideEditorTitle")->setText(QStringLiteral("Welcome slides"));
        auto *actions = control<QDialogButtonBox>(dialog, "slideEditorActions");
        QTest::mouseClick(actions->button(QDialogButtonBox::Save), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        SlideEditorDialog existing(example());
        QVERIFY(!existing.findChild<QPushButton *>(QStringLiteral("slideEditorAddSlide")));
    }

    void richPasteRetainsSupportedTextAndStripsEmbeddedResources() {
        SlideEditorDialog dialog(example());
        dialog.show();
        auto *item = textItem(dialog);
        select(dialog, 0, item->document()->toPlainText().size());
        auto *mime = new QMimeData;
        mime->setHtml(QStringLiteral("<p><span style='color:#ff0000;font-style:italic'>Pasted words</span>"
                                    "<img src='file:///private/secret.png'/></p>"));
        QApplication::clipboard()->setMimeData(mime);
        auto *canvas = control<QGraphicsView>(dialog, "slideTextCanvas");
        canvas->setFocus();
        item->setFocus();
        QTest::keySequence(canvas->viewport(), QKeySequence::Paste);
        QCOMPARE(item->document()->toPlainText(), QStringLiteral("Pasted words"));
        QVERIFY(!dialog.richText().first().contains(QStringLiteral("file:///private/secret.png")));
        QVERIFY(!dialog.richText().first().contains(QStringLiteral("<img")));
        QTextCursor cursor(item->document());
        cursor.setPosition(1);
        QVERIFY(cursor.charFormat().fontItalic());
        QCOMPARE(cursor.charFormat().foreground().color(), QColor(Qt::red));
    }

    void changingImportedPointSizePreservesMixedFormatting() {
        Song source = example();
        source.richText = {QStringLiteral("<p><span style='font-size:24pt;color:#ff0000'>First </span>"
                                         "<span style='font-size:24pt;font-style:italic;color:#00ff00'>words</span></p>"), QString()};
        SlideEditorDialog dialog(source);
        control<QSpinBox>(dialog, "slideEditorFontSize")->setValue(150);
        QTextDocument restored;
        RichTextSupport::load(&restored, dialog.slides().first(), dialog.richText().first());
        QTextCursor cursor(&restored);
        cursor.setPosition(1);
        QCOMPARE(cursor.charFormat().intProperty(QTextFormat::FontPixelSize), 150);
        QCOMPARE(cursor.charFormat().foreground().color(), QColor(Qt::red));
        cursor.setPosition(8);
        QCOMPARE(cursor.charFormat().intProperty(QTextFormat::FontPixelSize), 150);
        QCOMPARE(cursor.charFormat().foreground().color(), QColor(Qt::green));
        QVERIFY(cursor.charFormat().fontItalic());
    }

    void clickingBlankCanvasStartsTyping() {
        SlideEditorDialog dialog(Song{});
        dialog.show();
        QTest::qWait(20);
        auto *canvas = control<QGraphicsView>(dialog, "slideTextCanvas");
        const QPoint position = canvas->mapFromScene(QPointF(300, 200));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QTest::keyClicks(canvas->viewport(), QStringLiteral("Welcome"));
        QCOMPARE(dialog.slides().first(), QStringLiteral("Welcome"));
    }

    void toolbarReportsCurrentHtmlAndNavigatorUsesContainedCaptions() {
        SlideEditorDialog dialog(example());
        auto *item = textItem(dialog);
        item->setHtml(QStringLiteral("<p align='center'><span style='font-size:96px;color:#ffcc00'>Updated words</span></p>"));
        QCOMPARE(control<QSpinBox>(dialog, "slideEditorFontSize")->value(), 96);
        QVERIFY(control<QToolButton>(dialog, "slideAlignCenter")->isChecked());
        const auto color = control<QToolButton>(dialog, "slideTextColor")->icon().pixmap(16, 16).toImage().pixelColor(8, 8);
        QCOMPARE(color, QColor(QStringLiteral("#ffcc00")));
        auto *family = control<QFontComboBox>(dialog, "slideEditorFont");
        const QFont fallback = RichTextSupport::defaultFont();
        if (fallback.family().startsWith(QLatin1Char('.')) && family->findText(fallback.family(), Qt::MatchFixedString) < 0
            && family->findText(QFontInfo(fallback).family(), Qt::MatchFixedString) < 0)
            QCOMPARE(family->currentText(), QStringLiteral("System font"));
        auto *navigator = control<QListWidget>(dialog, "slideEditorSlides");
        QCOMPARE(navigator->viewMode(), QListView::IconMode);
        QCOMPARE(navigator->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(navigator->item(0)->text(), QStringLiteral("Slide 1"));
        QCOMPARE(navigator->item(0)->toolTip(), QStringLiteral("Updated words"));
    }
};

QTEST_MAIN(SlideEditorTest)
#include "slide_editor_test.moc"
