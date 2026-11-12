// Original WorshipCue artwork. Run this optional tool to reproduce the shipped
// stills; the application only needs the finished JPEGs, never this generator.
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTextStream>
#include <cmath>

namespace {
constexpr int Width = 3840;
constexpr int Height = 2160;
struct Artwork { const char *slug; const char *title; };
const Artwork collection[] = {
    {"aurora-veil", "Aurora Veil"}, {"midnight-tide", "Midnight Tide"},
    {"indigo-flow", "Indigo Flow"}, {"ember-glow", "Ember Glow"},
    {"jade-mist", "Jade Mist"}, {"violet-halo", "Violet Halo"},
    {"blue-hour", "Blue Hour"}, {"rose-dusk", "Rose Dusk"},
    {"golden-hour", "Golden Hour"}, {"arctic-light", "Arctic Light"},
    {"graphite-silk", "Graphite Silk"}, {"ocean-depths", "Ocean Depths"},
    {"plum-arc", "Plum Arc"}, {"copper-lines", "Copper Lines"},
    {"forest-light", "Forest Light"}, {"cobalt-ribbon", "Cobalt Ribbon"},
    {"desert-dusk", "Desert Dusk"}, {"lilac-drift", "Lilac Drift"},
    {"teal-contours", "Teal Contours"}, {"midnight-prism", "Midnight Prism"}
};

QColor color(const char *hex, int alpha = 255) {
    QColor result(QString::fromLatin1(hex));
    result.setAlpha(alpha);
    return result;
}

void base(QPainter &p, const char *first, const char *second, const char *third) {
    QLinearGradient gradient(0, 0, 1920, 1080);
    gradient.setColorAt(0, color(first));
    gradient.setColorAt(.52, color(second));
    gradient.setColorAt(1, color(third));
    p.fillRect(QRectF(0, 0, 1920, 1080), gradient);
}

void glow(QPainter &p, qreal x, qreal y, qreal rx, qreal ry,
          const char *hex, int alpha = 150) {
    p.save();
    p.translate(x, y);
    p.scale(rx, ry);
    QRadialGradient gradient(0, 0, 1);
    gradient.setColorAt(0, color(hex, alpha));
    gradient.setColorAt(.35, color(hex, int(alpha * .60)));
    gradient.setColorAt(.72, color(hex, int(alpha * .16)));
    gradient.setColorAt(1, color(hex, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawEllipse(QPointF(), 1, 1);
    p.restore();
}

void ribbon(QPainter &p, const QPainterPath &shape, const QPointF &from,
            const QPointF &to, const char *edge, const char *body, int alpha = 200) {
    QLinearGradient gradient(from, to);
    gradient.setColorAt(0, color(edge, alpha));
    gradient.setColorAt(.16, color(body, int(alpha * .75)));
    gradient.setColorAt(.6, color(body, int(alpha * .24)));
    gradient.setColorAt(1, color(body, 0));
    p.fillPath(shape, gradient);
}

QPainterPath wave(qreal y, qreal rise, qreal depth) {
    QPainterPath path;
    path.moveTo(-100, y);
    path.cubicTo(440, y - rise, 1120, y + rise, 2020, y - rise * .55);
    path.lineTo(2020, y + depth);
    path.cubicTo(1170, y + rise + depth, 470, y - rise + depth, -100, y + depth);
    path.closeSubpath();
    return path;
}

void orbit(QPainter &p, qreal x, qreal y, qreal rx, qreal ry,
           const char *hex, qreal width = 1.5, int alpha = 100) {
    QLinearGradient gradient(x - rx, y - ry, x + rx, y + ry);
    gradient.setColorAt(0, color(hex, 0));
    gradient.setColorAt(.3, color(hex, alpha));
    gradient.setColorAt(.65, color(hex, int(alpha * .3)));
    gradient.setColorAt(1, color(hex, 0));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(gradient), width));
    p.drawEllipse(QPointF(x, y), rx, ry);
    p.setPen(Qt::NoPen);
}

void foldedFlow(QPainter &p, const char *light, const char *shade, bool reverse = false) {
    p.save();
    if (reverse) { p.translate(1920, 1080); p.rotate(180); }
    for (int layer = 0; layer < 5; ++layer) {
        const qreal d = layer * 65;
        QPainterPath path;
        path.moveTo(-100, -100);
        path.lineTo(1060 - d, -100);
        path.cubicTo(580 - d, 100 + d * .3, 340 + d * .3, 280 + d, -100, 690 + d);
        path.closeSubpath();
        ribbon(p, path, QPointF(470 - d, 140 + d), QPointF(160 - d, -200),
               light, shade, 200 - layer * 22);
    }
    p.restore();
}

void artwork(QPainter &p, int index) {
    p.setPen(Qt::NoPen);
    switch (index) {
    case 0: {
        base(p, "#102437", "#101e35", "#162b42");
        glow(p, 280, 40, 1000, 740, "#27b99c", 150);
        glow(p, 1880, 1080, 1000, 640, "#5a66c5", 145);
        for (int i = 0; i < 7; ++i) {
            auto path = wave(125 + i * 20, 150 + i * 14, 95);
            ribbon(p, path, {300, 100 + i * 20.0}, {640, 340 + i * 20.0}, "#79d4c0", "#2d827e", 90 - i * 8);
        }
        break;
    }
    case 1:
        base(p, "#101d33", "#152c43", "#123a4b");
        glow(p, 1870, 1000, 940, 590, "#24a3b7", 150);
        for (int i = 0; i < 6; ++i)
            ribbon(p, wave(710 + i * 66, 150, 300), {800, 690 + i * 66.0}, {800, 970 + i * 66.0},
                   i % 2 ? "#355f8c" : "#459aaf", "#102941", 180);
        break;
    case 2:
        base(p, "#181c43", "#141d3b", "#212b60");
        glow(p, 1800, 1080, 900, 700, "#6263e1", 150);
        foldedFlow(p, "#8e8de7", "#3a4ba0");
        foldedFlow(p, "#6a84de", "#273c7b", true);
        break;
    case 3:
        base(p, "#301d36", "#251e2e", "#593039");
        glow(p, 1740, 1060, 1100, 930, "#d47b4c", 200);
        glow(p, 50, -50, 800, 640, "#9a4565", 95);
        for (int i = 0; i < 4; ++i) orbit(p, 2040, 1230, 700 + i * 90, 570 + i * 60, "#f1ac6b", 1.1, 65);
        break;
    case 4:
        base(p, "#152f32", "#15332f", "#244a41");
        glow(p, 140, 50, 950, 650, "#68bca0", 130);
        glow(p, 1940, 1050, 1100, 660, "#2f8d82", 160);
        for (int i = 0; i < 3; ++i)
            ribbon(p, wave(160 + i * 60, 210, 190), {300, 110 + i * 60.0}, {460, 380 + i * 60.0}, "#9ac1a7", "#3e7b65", 60);
        break;
    case 5:
        base(p, "#211d3e", "#211e37", "#302345");
        glow(p, 1900, 450, 900, 900, "#7544b6", 175);
        glow(p, 50, 1080, 900, 600, "#4c518b", 105);
        for (int i = 0; i < 8; ++i)
            orbit(p, 1970, 425, 630 + i * 9, 630 + i * 9, "#bd94eb", 3 + i * 4, 110 - i * 13);
        orbit(p, 1970, 425, 625, 625, "#ddc0ff", 1.6, 190);
        break;
    case 6: {
        base(p, "#122841", "#18374f", "#274b65");
        glow(p, 960, 760, 1500, 340, "#6aabc8", 120);
        for (int i = 0; i < 5; ++i) {
            QPainterPath ridge;
            const qreal y = 740 + i * 65;
            ridge.moveTo(-10, y + 70);
            ridge.cubicTo(340, y - 100, 430, y + 80, 860, y);
            ridge.cubicTo(1220, y - 100, 1410, y + 100, 1940, y - 40);
            ridge.lineTo(1940, 1090); ridge.lineTo(-10, 1090); ridge.closeSubpath();
            p.fillPath(ridge, color(i == 0 ? "#2d566d" : i == 1 ? "#23435b" : i == 2 ? "#1c354e" : i == 3 ? "#172c42" : "#122238"));
        }
        break;
    }
    case 7:
        base(p, "#3e2548", "#302640", "#392d4c");
        glow(p, 1750, -70, 1100, 850, "#d38f8b", 185);
        glow(p, 20, 1120, 1000, 740, "#9a628c", 180);
        ribbon(p, wave(1020, 400, 400), {300, 820}, {800, 1260}, "#c087a3", "#4b385a", 165);
        break;
    case 8:
        base(p, "#3b3031", "#302a2c", "#4a3a32");
        glow(p, -100, -50, 1300, 1050, "#d7af71", 200);
        glow(p, 1920, 1100, 1100, 800, "#9b6f53", 100);
        for (int i = 0; i < 5; ++i) orbit(p, -250, -360, 1000 + i * 115, 1000 + i * 115, "#ecd6a4", 1.2, 75);
        break;
    case 9: {
        base(p, "#142b43", "#1a3045", "#284756");
        glow(p, 1890, 20, 1040, 850, "#a5c9d7", 170);
        glow(p, 40, 1060, 950, 620, "#477e93", 120);
        QPainterPath beam;
        beam.moveTo(1560, -100); beam.lineTo(2020, -100);
        beam.lineTo(1300, 1120); beam.lineTo(1020, 1120); beam.closeSubpath();
        ribbon(p, beam, {1770, -50}, {1330, 880}, "#c6dfe1", "#6198b4", 90);
        foldedFlow(p, "#729fb9", "#2c5b7a");
        break;
    }
    case 10:
        base(p, "#252a31", "#1d222a", "#333b45");
        glow(p, 1900, 1080, 1050, 800, "#687482", 90);
        foldedFlow(p, "#a3abb0", "#535e6a");
        foldedFlow(p, "#7f8b93", "#3e4a58", true);
        break;
    case 11:
        base(p, "#102d40", "#123046", "#173a50");
        glow(p, -100, 1080, 1600, 1100, "#13899a", 200);
        glow(p, 1780, -140, 950, 860, "#27658d", 120);
        for (int i = 0; i < 4; ++i) orbit(p, -300, 1430, 1100 + i * 65, 720 + i * 65, "#74b0b4", 1, 45);
        break;
    case 12:
        base(p, "#37233d", "#2f2337", "#4d2b47");
        glow(p, 1720, 1150, 1000, 900, "#a35a8a", 130);
        for (int i = 0; i < 9; ++i)
            orbit(p, 1950, 1100, 720 + i * 32, 960 + i * 32, "#c28cae", i == 0 ? 2 : 1, 125 - i * 9);
        glow(p, 30, 20, 900, 620, "#714d75", 90);
        break;
    case 13: {
        base(p, "#2b2b30", "#28282f", "#353037");
        glow(p, -40, 1100, 1000, 760, "#ac775b", 90);
        for (int i = 0; i < 28; ++i) {
            QPainterPath contour;
            contour.moveTo(-80, 520 + i * 20);
            contour.cubicTo(280, 450 + i * 12, 350, 1050 + i * 12, 900 + i * 28, 1130);
            QLinearGradient line(0, 680, 900, 1080);
            line.setColorAt(0, color("#c99268", 100));
            line.setColorAt(.65, color("#b7866c", 55));
            line.setColorAt(1, color("#b7866c", 0));
            p.setPen(QPen(QBrush(line), 1.1)); p.drawPath(contour);
        }
        p.setPen(Qt::NoPen);
        break;
    }
    case 14: {
        base(p, "#203d37", "#192d2c", "#193b38");
        glow(p, 160, -180, 1150, 1100, "#85a88b", 160);
        glow(p, 1910, 1100, 1000, 800, "#23736b", 120);
        for (int i = 0; i < 5; ++i) {
            QPainterPath shaft;
            shaft.moveTo(100 + i * 120, -100); shaft.lineTo(150 + i * 130, -100);
            shaft.lineTo(1200 + i * 130, 1200); shaft.lineTo(850 + i * 120, 1200); shaft.closeSubpath();
            ribbon(p, shaft, {200 + i * 120.0, -50}, {1000, 950}, "#b5c29c", "#698b74", 40);
        }
        break;
    }
    case 15:
        base(p, "#101f3b", "#14243e", "#133365");
        glow(p, 1920, 1070, 1000, 800, "#245ce7", 185);
        foldedFlow(p, "#578ad6", "#1a497f");
        foldedFlow(p, "#558cf4", "#1b4aaf", true);
        break;
    case 16: {
        base(p, "#3c3449", "#443849", "#59404b");
        glow(p, 1540, 340, 1100, 700, "#c68e80", 120);
        for (int i = 0; i < 5; ++i)
            ribbon(p, wave(715 + i * 92, 140 + i * 20, 430), {600, 680 + i * 92.0}, {680, 980 + i * 92.0},
                   i == 0 ? "#b99487" : i == 1 ? "#987577" : i == 2 ? "#785a6d" : "#59475f", "#342d43", 255);
        break;
    }
    case 17:
        base(p, "#363650", "#282b45", "#343656");
        glow(p, 1700, -100, 1100, 950, "#bda9d0", 175);
        glow(p, 50, 1050, 1150, 750, "#7084c6", 170);
        ribbon(p, wave(1070, 550, 500), {200, 750}, {700, 1220}, "#a8b2dc", "#5d669c", 110);
        break;
    case 18: {
        base(p, "#13373d", "#183038", "#214750");
        glow(p, 1940, 1080, 850, 900, "#3c918b", 120);
        for (int i = 0; i < 34; ++i) {
            QPainterPath contour;
            contour.moveTo(2040, 140 + i * 21);
            contour.cubicTo(1380 - i * 9, 210 + i * 16, 2030 - i * 13, 920, 1040 - i * 17, 1180);
            QLinearGradient line(1850, 250, 1350, 1100);
            line.setColorAt(0, color("#70c1b7", 50));
            line.setColorAt(.4, color("#70c1b7", 100));
            line.setColorAt(1, color("#70c1b7", 0));
            p.setPen(QPen(QBrush(line), 1.2)); p.drawPath(contour);
        }
        p.setPen(Qt::NoPen);
        break;
    }
    case 19: {
        base(p, "#19263c", "#1a243a", "#253852");
        glow(p, 1950, -50, 950, 800, "#626ab0", 135);
        const QPointF points[][4] = {
            {{-20,-20}, {650,-20}, {270,330}, {-20,700}},
            {{-20,-20}, {430,180}, {270,330}, {-20,700}},
            {{1370,-20}, {1940,-20}, {1940,580}, {1740,400}},
            {{1940,580}, {1940,1100}, {1210,1100}, {1740,400}},
            {{1740,400}, {1560,1100}, {1210,1100}, {1650,790}}
        };
        for (int i = 0; i < 5; ++i) {
            QPainterPath facet; facet.addPolygon(QPolygonF{points[i][0], points[i][1], points[i][2], points[i][3]});
            facet.closeSubpath();
            ribbon(p, facet, points[i][0], points[i][2], i % 2 ? "#51899b" : "#6b79aa", "#2d4971", 135);
        }
        break;
    }
    }
    // A very broad, gentle vignette keeps the lyric field calm without flattening
    // the peripheral artwork. The slide renderer adds its usual dimming layer.
    glow(p, 960, 540, 850, 590, "#08111e", 28);
}

void dither(QImage &image, unsigned seed) {
    // One-level grain prevents visible bands on large projection screens. No
    // external textures or random state: every asset is reproducible.
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            unsigned hash = unsigned(x) * 374761393u + unsigned(y) * 668265263u + seed * 1013904223u;
            hash = (hash ^ (hash >> 13)) * 1274126177u;
            const int noise = int((hash ^ (hash >> 16)) % 3) - 1;
            const QRgb px = row[x];
            row[x] = qRgb(qBound(0, qRed(px) + noise, 255), qBound(0, qGreen(px) + noise, 255), qBound(0, qBlue(px) + noise, 255));
        }
    }
}
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    if (argc < 2 || argc > 3) {
        QTextStream(stderr) << "Usage: generate-backgrounds OUTPUT_DIRECTORY [CONTACT_SHEET.jpg]\n";
        return 1;
    }
    const QDir output(QString::fromLocal8Bit(argv[1]));
    if (!QDir().mkpath(output.absolutePath())) return 1;
    QImage sheet(1600, 1340, QImage::Format_RGB32);
    sheet.fill(color("#101721"));
    QPainter tiles(&sheet);
    tiles.setRenderHint(QPainter::SmoothPixmapTransform);
    tiles.setRenderHint(QPainter::TextAntialiasing);
    QFont label;
    label.setPixelSize(17); label.setWeight(QFont::Medium);
    tiles.setFont(label);
    for (int i = 0; i < 20; ++i) {
        QImage image(Width, Height, QImage::Format_RGB32);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(2, 2);
        artwork(p, i);
        p.end();
        dither(image, unsigned(i + 1));
        const QString file = output.filePath(QString::fromLatin1(collection[i].slug) + QStringLiteral(".jpg"));
        QImageWriter writer(file, "jpeg");
        writer.setQuality(94);
        writer.setOptimizedWrite(true);
        writer.setProgressiveScanWrite(true);
        if (!writer.write(image)) { QTextStream(stderr) << writer.errorString() << '\n'; return 1; }
        const QRect tile(16 + (i % 4) * 396, 16 + (i / 4) * 264, 380, 214);
        tiles.drawImage(tile, image.scaled(tile.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        tiles.setPen(color("#d7e1ee"));
        tiles.drawText(QRect(tile.x(), tile.bottom() + 7, tile.width(), 28), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(QString::fromLatin1(collection[i].title)));
        QTextStream(stdout) << file << '\n';
    }
    tiles.end();
    if (argc == 3 && !sheet.save(QString::fromLocal8Bit(argv[2]), "JPEG", 94)) return 1;
    return 0;
}
