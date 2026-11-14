#include "OutputConfiguration.h"
#include <QtMath>

QString outputName(OutputId id) {
    switch (id) {
    case OutputId::Audience1: return QStringLiteral("Audience Screen 1");
    case OutputId::Audience2: return QStringLiteral("Audience Screen 2");
    case OutputId::Stage1: return QStringLiteral("Stage Screen 1");
    case OutputId::Stage2: return QStringLiteral("Stage Screen 2");
    }
    return {};
}

bool OutputConfiguration::validate(QString *error) const {
    if (error) error->clear();
    const auto reject = [error](const QString &text) { if (error) *error = text; return false; };
    if (canvas.width() < 160 || canvas.height() < 90 || canvas.width() > 8192 || canvas.height() > 8192
        || qint64(canvas.width()) * canvas.height() > 33554432)
        return reject(QStringLiteral("Use a logical canvas between 160×90 and 8192×8192, with at most 32 megapixels."));
    if (aspectWidth < 1 || aspectHeight < 1 || aspectWidth > 10000 || aspectHeight > 10000
        || qreal(aspectWidth) / aspectHeight < .1 || qreal(aspectWidth) / aspectHeight > 10)
        return reject(QStringLiteral("Use a positive aspect ratio between 1:10 and 10:1."));
    if ((rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        || int(scaling) < 0 || int(scaling) > 2)
        return reject(QStringLiteral("Choose a supported orientation and scaling mode."));
    if ((target != "off" && target != "preview" && !target.startsWith("screen:")) || target.size() > 2048
        || signalMode.size() > 256 || stageLayout.isEmpty() || stageLayout.size() > 64)
        return reject(QStringLiteral("Invalid output route, display mode, or stage layout."));
    if (!signalMode.isEmpty() && !target.startsWith("screen:"))
        return reject(QStringLiteral("A physical signal mode requires a fullscreen display assignment. Preview uses a logical canvas only."));
    return true;
}

QJsonObject OutputConfiguration::toJson() const {
    return {{"target", target}, {"width", canvas.width()}, {"height", canvas.height()}, {"aspect_width", aspectWidth},
        {"aspect_height", aspectHeight}, {"rotation", rotation}, {"scaling", int(scaling)}, {"signal", signalMode}, {"layout", stageLayout}};
}

bool OutputConfiguration::fromJson(const QJsonObject &json, OutputConfiguration *result, QString *error) {
    OutputConfiguration parsed;
    for (const auto &key : {"width", "height", "aspect_width", "aspect_height", "rotation", "scaling"}) {
        const auto value = json.value(QString::fromLatin1(key));
        if (!value.isDouble() || value.toDouble() != value.toInt(-1)) {
            if (error) *error = QStringLiteral("Output dimensions and format settings must be whole numbers."); return false;
        }
    }
    for (const auto &key : {"target", "signal", "layout"}) if (!json.value(QString::fromLatin1(key)).isString()) {
        if (error) *error = QStringLiteral("Invalid saved output configuration."); return false;
    }
    parsed.target = json["target"].toString(); parsed.canvas = {json["width"].toInt(), json["height"].toInt()};
    parsed.aspectWidth = json["aspect_width"].toInt(); parsed.aspectHeight = json["aspect_height"].toInt();
    parsed.rotation = json["rotation"].toInt(); parsed.scaling = Scaling(json["scaling"].toInt());
    parsed.signalMode = json["signal"].toString(); parsed.stageLayout = json["layout"].toString();
    if (!parsed.validate(error)) return false;
    *result = parsed; return true;
}

QRectF outputContentRect(const QRectF &destination, qreal aspect, OutputConfiguration::Scaling scaling) {
    if (destination.isEmpty() || !qIsFinite(aspect) || aspect <= 0) return {};
    if (scaling == OutputConfiguration::Scaling::Stretch) return destination;
    QSizeF size(aspect, 1);
    const qreal scale = scaling == OutputConfiguration::Scaling::Fit
        ? qMin(destination.width() / size.width(), destination.height())
        : qMax(destination.width() / size.width(), destination.height());
    size *= scale;
    return {destination.center().x() - size.width() / 2, destination.center().y() - size.height() / 2, size.width(), size.height()};
}
