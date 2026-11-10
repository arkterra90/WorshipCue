#include "../src/ui/DisplayWindow.h"
#include "../src/ui/SlideRenderer.h"
#include "../src/media/MediaPlayback.h"

#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
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

private slots:
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
        const QImage normal = render(state, QSize(320, 180), {}, picture);
        QCOMPARE(normal.pixelColor(160, 90), QColor(Qt::red));
        QCOMPARE(normal.pixelColor(0, 90), QColor(Qt::black));
        state.clear = true;
        QCOMPARE(render(state, QSize(320, 180), {}, picture), normal);
        state.blackout = true;
        QCOMPARE(render(state, QSize(320, 180), {}, picture).pixelColor(160, 90), QColor(Qt::black));
    }

    void missingMediaFrameProjectsBlackWithoutFileInformation() {
        LivePresentation state;
        state.slides = {QString{}};
        state.media = {{MediaKind::Video, QStringLiteral("/private/file/path.mov"), QStringLiteral("Video"), false}};
        state.index = 0;
        const QImage result = render(state, QSize(320, 180));
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                QCOMPARE(result.pixelColor(x, y), QColor(Qt::black));
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
