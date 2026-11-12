#include "../src/ui/DisplayWindow.h"
#include "../src/ui/SlideRenderer.h"
#include "../src/ui/RichTextSupport.h"
#include "../src/ui/BackgroundLibraryDialog.h"
#include "../src/media/MediaPlayback.h"
#include "../src/core/BuiltInBackgrounds.h"

#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest/QtTest>

class SlideRendererTest : public QObject {
    Q_OBJECT
private:
    static QImage render(const LivePresentation &state, const QSize &size,
                         const QPixmap &background = {}, const QPixmap &frame = {}) {
        QImage result(size, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::magenta);
        QPainter painter(&result);
        SlideRenderer::paintAudience(painter, result.rect(), state, background, frame);
        return result;
    }
    static QImage composite(const QPixmap &outgoing, const QPixmap &incoming,
                            SlideTransitionType type, qreal progress,
                            const QSize &size = QSize(320, 180)) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::magenta);
        QPainter painter(&image);
        SlideRenderer::paintTransition(painter, image.rect(), outgoing, incoming, type, progress);
        return image;
    }
    static QImage audienceImage(DisplayWindow &audience, const QSize &size = QSize(320, 180)) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::magenta);
        QPainter painter(&image);
        audience.paintAudience(painter, image.rect());
        return image;
    }
    static QRect redTextBounds(const QImage &image) {
        QRect bounds;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 180 && color.green() < 80 && color.blue() < 80)
                    bounds |= QRect(x, y, 1, 1);
            }
        }
        return bounds;
    }

private slots:
    void richTextKeepsCharacterStylesAndParagraphAlignment() {
        const QString html = QStringLiteral(
            "<p align='right'><span style=\"font-family:'Helvetica'; font-size:96px; "
            "font-weight:700; font-style:italic; text-decoration:underline; color:#ff0000;\">"
            "Glory &amp; light</span></p>");
        QTextDocument document;
        RichTextSupport::load(&document, QString(), html);
        QCOMPARE(document.toPlainText(), QStringLiteral("Glory & light"));
        QCOMPARE(document.documentMargin(), qreal(0));
        QVERIFY(document.firstBlock().blockFormat().alignment().testFlag(Qt::AlignRight));
        QTextCursor cursor(&document);
        cursor.setPosition(1);
        const QTextCharFormat format = cursor.charFormat();
        QCOMPARE(format.foreground().color(), QColor(Qt::red));
        QCOMPARE(format.font().pixelSize(), 96);
        QVERIFY(format.fontWeight() >= QFont::Bold);
        QVERIFY(format.fontItalic());
        QVERIFY(format.fontUnderline());
        QVERIFY(format.fontFamilies().toStringList().contains(QStringLiteral("Helvetica")));
    }

    void richTextSizesColorsAndAlignmentReachAudienceAndThumbnails() {
        LivePresentation state;
        state.slides = {QStringLiteral("WORD")};
        state.index = 0;
        state.richText = {QStringLiteral("<p align='left' style='font-size:144px;color:#ff0000'>WORD</p>")};
        const QSize size(640, 360);
        const QImage left = render(state, size);
        const QRect leftBounds = redTextBounds(left);
        QVERIFY(!leftBounds.isEmpty());
        QCOMPARE(SlideRenderer::thumbnail(state.currentText(), size, {}, state.currentRichText())
            .toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied), left);
        state.richText = {QStringLiteral("<p align='right' style='font-size:144px;color:#ff0000'>WORD</p>")};
        const QRect rightBounds = redTextBounds(render(state, size));
        QVERIFY(rightBounds.left() > leftBounds.right());
        state.richText = {QStringLiteral("<p align='left' style='font-size:36px;color:#ff0000'>WORD</p>")};
        const QRect small = redTextBounds(render(state, size));
        QVERIFY(leftBounds.height() > small.height() * 2);
        QVERIFY(leftBounds.width() > small.width() * 2);

        state.clear = true;
        const QImage cleared = render(state, size);
        QVERIFY(redTextBounds(cleared).isEmpty());
        state.richText.clear();
        state.slides = {QString()};
        state.clear = false;
        QCOMPARE(render(state, size), cleared);
        state.blackout = true;
        QCOMPARE(render(state, size).pixelColor(320, 180), QColor(Qt::black));
    }

    void richTextLongSlidesShrinkWithinTheSameCanvasMargins() {
        LivePresentation state;
        state.slides = {QStringLiteral("Long text")};
        state.index = 0;
        QStringList lines;
        for (int i = 0; i < 18; ++i) lines.append(QStringLiteral("<p style='font-size:180px;color:#ff0000'>Line %1</p>").arg(i + 1));
        state.richText = {lines.join(QString())};
        const QRect bounds = redTextBounds(render(state, QSize(1920, 1080)));
        QVERIFY(!bounds.isEmpty());
        QVERIFY(bounds.top() >= 130);
        QVERIFY(bounds.bottom() < 950);
        QVERIFY(bounds.left() >= 154);
        QVERIFY(bounds.right() < 1766);
        QVERIFY(bounds.height() > 700);
    }

    void pastedHtmlCannotLoadImagesOrKeepResourceReferences() {
        class ResourceSpy final : public QTextDocument {
        public:
            int loads = 0;
        protected:
            QVariant loadResource(int, const QUrl &) override { ++loads; return {}; }
        } document;
        const QString input = QStringLiteral(
            "<p align='center'><a href='https://example.invalid'>Visible text</a>"
            "<img src='file:///private/not-a-slide-image.png' />"
            "<img src='https://example.invalid/tracking.png' /></p>");
        const QString clean = RichTextSupport::sanitize(input);
        QVERIFY(!clean.contains(QStringLiteral("<img"), Qt::CaseInsensitive));
        QVERIFY(!clean.contains(QStringLiteral("href="), Qt::CaseInsensitive));
        QVERIFY(!clean.contains(QStringLiteral("file:///")));
        QVERIFY(!clean.contains(QStringLiteral("example.invalid")));
        RichTextSupport::load(&document, QStringLiteral("Fallback"), input);
        document.setTextWidth(1612);
        document.size();
        QCOMPARE(document.loads, 0);
        QCOMPARE(document.toPlainText().trimmed(), QStringLiteral("Visible text"));
        RichTextSupport::load(&document, QStringLiteral("Plain\nsecond line"), QString());
        QCOMPARE(document.toPlainText(), QStringLiteral("Plain\nsecond line"));
        QCOMPARE(document.firstBlock().blockFormat().alignment(), Qt::AlignHCenter);
        QTextDocument lineBreaks;
        lineBreaks.setPlainText(QStringLiteral("First\u2028Second\u2029Third"));
        RichTextSupport::load(&document, QString(), lineBreaks.toHtml());
        QCOMPARE(document.toPlainText(), QStringLiteral("First\nSecond\nThird"));
    }

    void transitionEndpointsAndFadeMixture() {
        QPixmap red(320, 180), blue(320, 180);
        red.fill(Qt::red);
        blue.fill(Qt::blue);
        for (const auto type : {SlideTransitionType::Fade, SlideTransitionType::SlideLeft, SlideTransitionType::SlideRight}) {
            QCOMPARE(composite(red, blue, type, 0).pixelColor(160, 90), QColor(Qt::red));
            QCOMPARE(composite(red, blue, type, 1).pixelColor(160, 90), QColor(Qt::blue));
        }
        QCOMPARE(composite(red, blue, SlideTransitionType::Cut, 0).pixelColor(160, 90), QColor(Qt::blue));
        const QColor middle = composite(red, blue, SlideTransitionType::Fade, .5).pixelColor(160, 90);
        // Qt's raster opacity uses fixed-point sampling (Qt 6.12 produces
        // 129 red / 126 blue here); both colors retain half the intensity.
        QVERIFY(qAbs(middle.red() - 128) <= 2);
        QVERIFY(qAbs(middle.blue() - 128) <= 2);
        QCOMPARE(middle.red() + middle.blue(), 255);
        QCOMPARE(middle.green(), 0);
    }

    void slideDirectionsAndLetterboxes() {
        QPixmap red(320, 180), blue(320, 180);
        red.fill(Qt::red);
        blue.fill(Qt::blue);
        const QImage left = composite(red, blue, SlideTransitionType::SlideLeft, .5, QSize(320, 320));
        QCOMPARE(left.pixelColor(80, 160), QColor(Qt::red));
        QCOMPARE(left.pixelColor(240, 160), QColor(Qt::blue));
        QCOMPARE(left.pixelColor(160, 0), QColor(Qt::black));
        QCOMPARE(left.pixelColor(160, 319), QColor(Qt::black));
        const QImage right = composite(red, blue, SlideTransitionType::SlideRight, .5);
        QCOMPARE(right.pixelColor(80, 90), QColor(Qt::blue));
        QCOMPARE(right.pixelColor(240, 90), QColor(Qt::red));
    }

    void zeroDurationCutsImmediately() {
        PresentationController controller;
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        PresentationSettings settings;
        settings.transition = SlideTransitionType::Fade;
        settings.transitionDurationMs = 0;
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("First"), QStringLiteral("Second")}, 0, settings);
        const QImage first = audienceImage(audience);
        QVERIFY(controller.next());
        const QImage second = audienceImage(audience);
        QVERIFY(second != first);
        DisplayWindow reference(DisplayWindow::Kind::Audience, &controller);
        QCOMPARE(second, audienceImage(reference));
        QSignalSpy frames(&audience, &DisplayWindow::audienceFrameChanged);
        QTest::qWait(80);
        QCOMPARE(frames.count(), 0);
        QCOMPARE(audienceImage(audience), second);
    }

    void rapidCueKeepsCurrentCompositeAndOutputControlsAreImmediate() {
        PresentationController controller;
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        PresentationSettings settings;
        settings.transition = SlideTransitionType::Fade;
        settings.transitionDurationMs = 1000;
        controller.takeLive(QStringLiteral("Song"),
            {QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")}, 0, settings);
        QTest::qWait(1100);
        const QImage first = audienceImage(audience);
        QVERIFY(controller.next());
        QTest::qWait(200);
        const QImage middle = audienceImage(audience);
        QVERIFY(middle != first);
        // Both live surfaces can paint the same stored animation frame.
        QCOMPARE(audienceImage(audience), middle);
        QVERIFY(controller.next());
        QCOMPARE(audienceImage(audience), middle);
        controller.toggleBlackout();
        const QImage black = audienceImage(audience);
        QCOMPARE(black.pixelColor(160, 90), QColor(Qt::black));
        QTest::qWait(100);
        QCOMPARE(audienceImage(audience), black);
        controller.restoreOutput();
        const QImage restored = audienceImage(audience);
        QVERIFY(restored != middle);
        QVERIFY(restored != black);
        QTest::qWait(100);
        QCOMPARE(audienceImage(audience), restored);
        controller.toggleClear();
        const QImage cleared = audienceImage(audience);
        QVERIFY(cleared != restored);
        QTest::qWait(100);
        QCOMPARE(audienceImage(audience), cleared);
    }

    void mediaFadeWaitsForDecodedFrameAndSharesHiddenOutput() {
        QTemporaryDir directory;
        const QString redPath = directory.filePath(QStringLiteral("red.png"));
        const QString bluePath = directory.filePath(QStringLiteral("blue.png"));
        QImage picture(320, 180, QImage::Format_RGB32);
        picture.fill(Qt::red);
        QVERIFY(picture.save(redPath));
        picture.fill(Qt::blue);
        QVERIFY(picture.save(bluePath));
        PresentationController controller;
        // Exercise the opposite construction order from MainWindow as well.
        MediaPlayback playback(&controller);
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        audience.setMediaPlayback(&playback);
        PresentationSettings settings;
        settings.transition = SlideTransitionType::Fade;
        settings.transitionDurationMs = 300;
        PresentationItem item{11, QStringLiteral("Pictures"), {QString(), QString()},
            {{MediaKind::Image, redPath, QStringLiteral("Red"), false},
             {MediaKind::Image, bluePath, QStringLiteral("Blue"), false}}};
        item.settings = settings;
        controller.takeServiceLive(1, {item}, 11);
        QTRY_COMPARE_WITH_TIMEOUT(audienceImage(audience).pixelColor(160, 90), QColor(Qt::red), 3000);
        QVERIFY(!audience.isVisible());
        QVERIFY(controller.next());
        // No empty/black frame between decoding the next picture and its fade.
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), QColor(Qt::red));
        QTRY_VERIFY_WITH_TIMEOUT(audienceImage(audience).pixelColor(160, 90).blue() > 0, 3000);
        const QColor mixed = audienceImage(audience).pixelColor(160, 90);
        QVERIFY(mixed.red() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(audienceImage(audience).pixelColor(160, 90), QColor(Qt::blue), 3000);
        controller.previous();
        controller.toggleBlackout();
        QTRY_VERIFY_WITH_TIMEOUT(!playback.frame().isNull(), 3000);
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), QColor(Qt::black));
    }

    void picturesRetainFullBrightnessAndFitWithoutCropping() {
        LivePresentation state;
        state.slides = {QString{}};
        state.media = {{MediaKind::Image, QStringLiteral("/not-read-in-paint.jpg"), QStringLiteral("Picture"), false}};
        state.index = 0;
        QPixmap picture(100, 100);
        picture.fill(Qt::red);
        QPixmap background(320, 180);
        background.fill(Qt::blue);
        const QImage normal = render(state, QSize(320, 180), background, picture);
        QCOMPARE(normal.pixelColor(160, 90), QColor(Qt::red));
        QVERIFY(normal.pixelColor(0, 90).blue() > 100);
        QCOMPARE(normal.pixelColor(0, 90).red(), 0);
        state.clear = true;
        QCOMPARE(render(state, QSize(320, 180), background, picture), normal);
        QPixmap fullCanvas(320, 180);
        fullCanvas.fill(Qt::red);
        const QImage fullScreen = render(state, QSize(320, 180), background, fullCanvas);
        QCOMPARE(fullScreen.pixelColor(0, 90), QColor(Qt::red));
        QCOMPARE(fullScreen.pixelColor(319, 90), QColor(Qt::red));
        state.blackout = true;
        QCOMPARE(render(state, QSize(320, 180), background, picture).pixelColor(160, 90), QColor(Qt::black));
    }

    void missingMediaFramePreservesBackgroundWithoutFileInformation() {
        LivePresentation state;
        state.slides = {QString{}};
        state.media = {{MediaKind::Video, QStringLiteral("/private/file/path.mov"), QStringLiteral("Video"), false}};
        state.index = 0;
        const QImage result = render(state, QSize(320, 180));
        state.media.clear();
        QCOMPARE(result, render(state, QSize(320, 180)));
    }

    void assignedBackgroundsFollowPresentationCuesAndTransitionTogether() {
        QTemporaryDir directory;
        const QString redPath = directory.filePath(QStringLiteral("red.png"));
        const QString bluePath = directory.filePath(QStringLiteral("blue.png"));
        QImage image(320, 180, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(redPath));
        image.fill(Qt::blue);
        QVERIFY(image.save(bluePath));
        PresentationController controller;
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        PresentationItem first{11, QStringLiteral("First"), {QString(), QString()}};
        first.backgroundPath = redPath;
        PresentationItem second{12, QStringLiteral("Second"), {QString()}};
        second.backgroundPath = bluePath;
        second.settings.transition = SlideTransitionType::Fade;
        second.settings.transitionDurationMs = 200;
        controller.takeServiceLive(1, {first, second}, 11);
        const QColor red = audienceImage(audience).pixelColor(160, 90);
        QVERIFY(red.red() > 100);
        QCOMPARE(red.blue(), 0);
        controller.next();
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), red);
        controller.next();
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), red);
        QTRY_VERIFY_WITH_TIMEOUT(audienceImage(audience).pixelColor(160, 90).blue() > 100, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(audienceImage(audience).pixelColor(160, 90).red(), 0, 1000);
        const QColor blue = audienceImage(audience).pixelColor(160, 90);
        QCOMPARE(blue.red(), 0);
        controller.toggleClear();
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), blue);
        controller.previous();
        QCOMPARE(audienceImage(audience).pixelColor(160, 90), red);
    }

    void builtInBackgroundsAreReadyWithoutImport() {
        QTemporaryDir directory;
        const QString databaseDirectory = directory.filePath(QStringLiteral("library"));
        BackgroundLibraryDialog dialog(databaseDirectory, {});
        auto *items = dialog.findChild<QListWidget *>(QStringLiteral("backgroundLibraryItems"));
        QVERIFY(items);
        QCOMPARE(BuiltInBackgrounds::catalog().size(), 20);
        QCOMPARE(items->count(), 21);
        QVERIFY(dialog.selectedPath().isEmpty());
        QSet<QString> titles;
        QSet<QString> paths;
        QSet<QByteArray> images;
        for (int i = 0; i < BuiltInBackgrounds::catalog().size(); ++i) {
            const auto &background = BuiltInBackgrounds::catalog().at(i);
            auto *item = items->item(i + 1);
            QCOMPARE(item->text(), background.title);
            QCOMPARE(item->data(Qt::UserRole).toString(), background.path);
            QVERIFY(!item->icon().isNull());
            QVERIFY(!titles.contains(background.title));
            QVERIFY(!paths.contains(background.path));
            titles.insert(background.title);
            paths.insert(background.path);
            const QString resource = BuiltInBackgrounds::resourcePath(background.path);
            QVERIFY(resource.startsWith(QStringLiteral(":/backgrounds/builtin/")));
            QImageReader reader(resource);
            QCOMPARE(reader.size(), QSize(3840, 2160));
            reader.setScaledSize(QSize(320, 180));
            QVERIFY2(!reader.read().isNull(), qPrintable(reader.errorString()));
            QFile file(resource);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray digest = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
            QVERIFY(!images.contains(digest));
            images.insert(digest);
        }
        const QString selected = BuiltInBackgrounds::catalog().last().path;
        items->setCurrentRow(20);
        QCOMPARE(dialog.selectedPath(), selected);
        BackgroundLibraryDialog reopened(databaseDirectory, selected);
        QCOMPARE(reopened.selectedPath(), selected);
        QCOMPARE(reopened.findChild<QListWidget *>(QStringLiteral("backgroundLibraryItems"))->count(), 21);
        // Included artwork remains available without creating a managed-image folder.
        QVERIFY(!QFileInfo::exists(databaseDirectory));
    }

    void importedBackgroundsAreManagedReusableAndLeaveOriginalsIntact() {
        QTemporaryDir directory;
        const QString source = directory.filePath(QStringLiteral("Source.jpg"));
        const QString databaseDirectory = directory.filePath(QStringLiteral("library"));
        QImage image(320, 180, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(source));
        BackgroundLibraryDialog dialog(databaseDirectory, {});
        QString error;
        QVERIFY2(dialog.importFiles({source}, &error), qPrintable(error));
        const QString first = dialog.selectedPath();
        QVERIFY(first.startsWith(QStringLiteral("backgrounds/")));
        QVERIFY(QFileInfo::exists(QDir(databaseDirectory).filePath(first)));
        QVERIFY(QFileInfo::exists(source));
        BackgroundLibraryDialog reopened(databaseDirectory, first);
        QCOMPARE(reopened.selectedPath(), first);
        auto *items = reopened.findChild<QListWidget *>(QStringLiteral("backgroundLibraryItems"));
        QVERIFY(items);
        QCOMPARE(items->count(), 22);
        QVERIFY2(reopened.importFiles({source}, &error), qPrintable(error));
        QVERIFY(reopened.selectedPath() != first);
        QCOMPARE(items->count(), 23);
        QFile bad(directory.filePath(QStringLiteral("broken.png")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        QVERIFY(!reopened.importFiles({bad.fileName()}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(items->count(), 23);
        reopened.setAssignmentEnabled(false);
        QVERIFY(!reopened.findChild<QPushButton *>(QStringLiteral("assignBackground"))->isEnabled());
    }

    void blackoutOverridesTextAndBackground() {
        LivePresentation state;
        state.slides = {QStringLiteral("Visible lyric")};
        state.index = 0;
        state.blackout = true;
        QPixmap background(32, 32);
        background.fill(Qt::red);
        const QImage result = render(state, QSize(320, 180), background);
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                QCOMPARE(result.pixelColor(x, y), QColor(Qt::black));
    }

    void clearPreservesOnlyBackground() {
        LivePresentation state;
        state.slides = {QStringLiteral("Hidden lyric")};
        state.index = 0;
        state.clear = true;
        QPixmap background(32, 32);
        background.fill(Qt::red);
        const QImage cleared = render(state, QSize(320, 180), background);
        state.slides = {QString{}};
        state.clear = false;
        QCOMPARE(cleared, render(state, QSize(320, 180), background));
        QVERIFY(cleared.pixelColor(160, 90).red() > 100);
        QCOMPARE(cleared.pixelColor(160, 90).green(), 0);
    }

    void audienceUsesSixteenByNineCanvas() {
        LivePresentation state;
        state.clear = true;
        QPixmap background(32, 32);
        background.fill(Qt::red);
        const QImage result = render(state, QSize(320, 320), background);
        QCOMPARE(result.pixelColor(160, 0), QColor(Qt::black));
        QCOMPARE(result.pixelColor(160, 319), QColor(Qt::black));
        QVERIFY(result.pixelColor(160, 160).red() > 100);
    }

    void thumbnailMatchesAudience() {
        const QString lyric = QStringLiteral("First line\nSecond line\nThird line");
        LivePresentation state;
        state.slides = {lyric};
        state.index = 0;
        const QSize size(480, 270);
        QCOMPARE(SlideRenderer::thumbnail(lyric, size).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
                 render(state, size));
        QVERIFY(SlideRenderer::thumbnail(lyric, QSize()).isNull());
    }

    void stageKeepsLyricsDuringAudienceClearAndBlackout() {
        PresentationController controller;
        controller.takeLive(QStringLiteral("Song"),
                            {QStringLiteral("Current lyrics"), QStringLiteral("Next lyrics")});
        DisplayWindow stage(DisplayWindow::Kind::Stage, &controller);
        stage.resize(640, 360);
        const auto lyricArea = [&stage] {
            const QPixmap image = stage.grab();
            const qreal ratio = image.devicePixelRatio();
            return image.toImage().copy(QRect(int(32 * ratio), int(100 * ratio),
                                               int(576 * ratio), int(115 * ratio)));
        };
        const QImage original = lyricArea();
        controller.toggleClear();
        QCOMPARE(lyricArea(), original);
        controller.toggleBlackout();
        QCOMPARE(lyricArea(), original);
    }

    void escapeRequestsOperatorWithoutChangingOutput() {
        PresentationController controller;
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("Lyrics")});
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        QSignalSpy focus(&audience, &DisplayWindow::operatorFocusRequested);
        audience.showFullScreen();
        QVERIFY(audience.isFullScreen());
        QTest::keyClick(&audience, Qt::Key_Escape);
        QCOMPARE(focus.count(), 1);
        QVERIFY(audience.isFullScreen());
        QCOMPARE(controller.state().currentText(), QStringLiteral("Lyrics"));
        QVERIFY(!controller.state().blackout);
        QSignalSpy closed(&audience, &DisplayWindow::closedByOperator);
        audience.close();
        QCOMPARE(closed.count(), 1);
    }
};

QTEST_MAIN(SlideRendererTest)
#include "slide_renderer_test.moc"
