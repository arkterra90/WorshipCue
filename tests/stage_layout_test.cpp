#include "../src/ui/DisplayWindow.h"
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class StageLayoutTest : public QObject {
    Q_OBJECT
    static QImage render(DisplayWindow &window, QSize size) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::magenta);
        QPainter painter(&image); window.paintOutput(painter, image.rect()); return image;
    }
private slots:
    void formatRoundTripAndBounds() {
        OutputConfiguration config; config.target = "preview"; config.canvas = {1080,1920}; config.aspectWidth = 9; config.aspectHeight = 16;
        config.rotation = 270; config.scaling = OutputConfiguration::Scaling::Fill;
        OutputConfiguration parsed; QString error;
        QVERIFY(OutputConfiguration::fromJson(config.toJson(), &parsed, &error)); QCOMPARE(parsed.toJson(), config.toJson());
        for (auto size : {QSize(0,1080), QSize(1920,0), QSize(8192,8192), QSize(9000,200)}) {
            auto invalid = config; invalid.canvas = size; QVERIFY(!invalid.validate(&error)); QVERIFY(!error.isEmpty());
        }
        auto invalid = config; invalid.rotation = 45; QVERIFY(!invalid.validate());
        invalid = config; invalid.aspectWidth = 0; QVERIFY(!invalid.validate());
        invalid = config; invalid.aspectWidth = 10000; invalid.aspectHeight = 1; QVERIFY(!invalid.validate());
        invalid = config; invalid.scaling = OutputConfiguration::Scaling(99); QVERIFY(!invalid.validate());
        invalid = config; invalid.signalMode = "1080i"; QVERIFY(!invalid.validate());
        auto json = config.toJson(); json["width"] = 1000.5; QVERIFY(!OutputConfiguration::fromJson(json, &parsed, &error));
    }
    void audienceOutputsShareContentWithIndependentFitCropStretchAndRotation() {
        PresentationController controller;
        DisplayWindow primary(DisplayWindow::Kind::Audience, &controller);
        DisplayWindow secondary(DisplayWindow::Kind::Audience, &controller, nullptr, &primary);
        PresentationItem item{0,"Red", {""}};
        PresentationLook red; red.background = PresentationLook::Background::Color; red.backgroundColor = "#ff0000";
        item.looks = {{1,red,false,false,{}}}; controller.takeLive(item);
        const auto revision = controller.state().cueRevision;
        auto image = render(secondary, {400,400});
        QCOMPARE(image.pixelColor(200,200), QColor(Qt::red)); QCOMPARE(image.pixelColor(0,0), QColor(Qt::black));
        auto config = secondary.configuration(); config.rotation = 90;
        QVERIFY(secondary.setConfiguration(config)); image = render(secondary, {400,400});
        QCOMPARE(image.pixelColor(200,200), QColor(Qt::red)); QCOMPARE(image.pixelColor(0,200), QColor(Qt::black));
        for (auto scaling : {OutputConfiguration::Scaling::Fill, OutputConfiguration::Scaling::Stretch}) {
            config.scaling = scaling; QVERIFY(secondary.setConfiguration(config)); image = render(secondary, {400,400});
            QCOMPARE(image.pixelColor(0,0), QColor(Qt::red)); QCOMPARE(image.pixelColor(399,399), QColor(Qt::red));
        }
        QCOMPARE(primary.configuration().rotation, 0); QCOMPARE(controller.state().cueRevision, revision);
        controller.toggleBlackout(); image = render(secondary, {400,400}); QCOMPARE(image.pixelColor(200,200), QColor(Qt::black));
    }
    void allRotationsPreserveContentOrientation_data() {
        QTest::addColumn<int>("rotation");
        for (int angle : {0,90,180,270}) QTest::newRow(qPrintable(QString::number(angle))) << angle;
    }
    void allRotationsPreserveContentOrientation() {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        QImage background(1920,1080, QImage::Format_RGB32); background.fill(Qt::red);
        { QPainter painter(&background); painter.fillRect(QRect(960,0,960,1080), Qt::blue); }
        const auto path = directory.filePath("orientation.png"); QVERIFY(background.save(path));
        PresentationController controller; DisplayWindow output(DisplayWindow::Kind::Audience, &controller);
        output.setBackgroundImage(path); controller.takeLive("Orientation", {QStringLiteral("")});
        auto config = output.configuration(); config.canvas = {320,180}; config.rotation = rotation;
        QVERIFY(output.setConfiguration(config));
        const auto image = render(output, rotation == 90 || rotation == 270 ? QSize(180,320) : QSize(320,180));
        const auto first = image.pixelColor(image.width()/4, image.height()/4);
        const auto second = image.pixelColor(3*image.width()/4, 3*image.height()/4);
        if (rotation == 0 || rotation == 90) { QVERIFY(first.red() > first.blue()); QVERIFY(second.blue() > second.red()); }
        else { QVERIFY(first.blue() > first.red()); QVERIFY(second.red() > second.blue()); }
    }

    void customStageRenderingAtDifferentCanvasFormats_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("HD") << QSize(1920,1080);
        QTest::newRow("4x3") << QSize(1024,768);
        QTest::newRow("portrait") << QSize(720,1280);
        QTest::newRow("wide") << QSize(1600,600);
    }
    void customStageRenderingAtDifferentCanvasFormats() {
        QFETCH(QSize, size);
        PresentationController controller; controller.takeLive("Service", {"Current lyrics", "Next lyrics"});
        DisplayWindow stage(DisplayWindow::Kind::Stage, &controller);
        auto layout = StageLayout::standard(); layout.id = "custom"; layout.background = "#123456";
        layout.elements = {{StageElement::Kind::CurrentSlide, {.1,.1,.8,.3}, {}, {}, 100, "#ffffff", true, 1},
            {StageElement::Kind::NextSlide, {.1,.6,.8,.2}, {}, {}, 80, "#ffff00", false, 1}};
        stage.setStageLayout(layout);
        auto config = stage.configuration(); config.canvas = size; config.aspectWidth = size.width(); config.aspectHeight = size.height();
        QVERIFY(stage.setConfiguration(config)); const auto image = render(stage, size);
        QCOMPARE(image.size(), size); QCOMPARE(image.pixelColor(0,0), QColor("#123456"));
        int white = 0, yellow = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x,y);
            if (pixel.red() > 220 && pixel.green() > 220 && pixel.blue() > 220) { ++white; QVERIFY(y >= .1 * size.height() && y <= .4 * size.height()); }
            if (pixel.red() > 220 && pixel.green() > 220 && pixel.blue() < 50) { ++yellow; QVERIFY(y >= .6 * size.height() && y <= .8 * size.height()); }
        }
        QVERIFY(white > 30); QVERIFY(yellow > 30);
        controller.toggleClear(); QCOMPARE(render(stage,size), image);
        controller.toggleBlackout(); QCOMPARE(render(stage,size), image);
    }
    void stageLayoutSerializationAndDynamicElements() {
        auto layout = StageLayout::standard(); layout.id = "editable"; layout.name = "Sunday";
        for (int kind = 3; kind <= 6; ++kind) layout.elements.append({StageElement::Kind(kind), {.1,.1,.8,.2}, "Custom words", "Arial", 64, "#ff0000", false, 0});
        QString error; StageLayout decoded;
        QVERIFY(StageLayout::fromJson(layout.toJson(), &decoded, &error)); QCOMPARE(decoded.toJson(), layout.toJson());
        LivePresentation state; state.index = 0; state.title = "Title"; state.slides = {"First", "Second"};
        StageRuntime runtime{"Message", 5500, false}; StageElement element;
        for (int kind = 0; kind <= 6; ++kind) {
            element.kind = StageElement::Kind(kind); element.text = "Custom";
            const auto text = stageElementText(element, state, runtime);
            QVERIFY(!text.isEmpty());
            if (kind == 4) QCOMPARE(text, QStringLiteral("00:06"));
            if (kind == 5) QCOMPARE(text, runtime.message);
        }
        auto invalid = layout; invalid.elements.first().rect = {.9,.9,.5,.5}; QVERIFY(!invalid.validate(&error));
        QVERIFY(!StageLayout::fromJson(invalid.toJson(), &decoded, &error));
        invalid = layout; invalid.elements.first().fontSize = 0; QVERIFY(!invalid.validate());
        invalid = layout; invalid.background = "invalid"; QVERIFY(!invalid.validate());
        invalid = layout; invalid.elements.first().kind = StageElement::Kind(55); QVERIFY(!invalid.validate());
        auto malformed = layout.toJson(); malformed["elements"] = "wrong type"; QVERIFY(!StageLayout::fromJson(malformed, &decoded, &error));
    }
    void stageTimersAndMessagesAreIndependentAndDoNotChangeTheCue() {
        PresentationController controller; controller.takeLive("Song", {"One", "Two"});
        const auto revision = controller.state().cueRevision;
        DisplayWindow first(DisplayWindow::Kind::Stage, &controller), second(DisplayWindow::Kind::Stage, &controller);
        QVERIFY(first.setStageMessage("First message")); QVERIFY(second.setStageMessage("Second message"));
        QVERIFY(!second.setStageMessage(QString(4001, 'a')));
        first.setCountdownSeconds(2); first.startCountdown(); QTest::qWait(60);
        QVERIFY(first.stageRuntime().remainingMs < 2000); QCOMPARE(second.stageRuntime().remainingMs, qint64(300000));
        first.pauseCountdown(); const auto remaining = first.stageRuntime().remainingMs; QTest::qWait(50);
        QCOMPARE(first.stageRuntime().remainingMs, remaining); first.resumeCountdown(); QTest::qWait(50);
        QVERIFY(first.stageRuntime().remainingMs < remaining); first.resetCountdown();
        QCOMPARE(first.stageRuntime().remainingMs, qint64(2000)); QVERIFY(!first.stageRuntime().running);
        QCOMPARE(first.stageRuntime().message, QStringLiteral("First message")); QCOMPARE(second.stageRuntime().message, QStringLiteral("Second message"));
        QCOMPARE(controller.state().cueRevision, revision); QCOMPARE(controller.state().index, 0);
    }
};
QTEST_MAIN(StageLayoutTest)
#include "stage_layout_test.moc"
