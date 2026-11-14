#include "StageLayout.h"
#include <QColor>
#include <QDateTime>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QPainter>
#include <QtMath>

QString StageLayout::elementName(StageElement::Kind kind) {
    switch (kind) {
    case StageElement::Kind::CurrentSlide: return "Current slide";
    case StageElement::Kind::NextSlide: return "Next slide";
    case StageElement::Kind::Title: return "Presentation title";
    case StageElement::Kind::Clock: return "Clock";
    case StageElement::Kind::Countdown: return "Countdown timer";
    case StageElement::Kind::Message: return "Stage message";
    case StageElement::Kind::CustomText: return "Custom text";
    }
    return {};
}
StageLayout StageLayout::standard() {
    StageLayout layout;
    layout.elements = {
        {StageElement::Kind::Title, {.03,.03,.65,.08}, {}, {}, 44, "#9ba5b4", true, 0},
        {StageElement::Kind::Clock, {.7,.03,.27,.08}, {}, {}, 44, "#9ba5b4", false, 2},
        {StageElement::Kind::CustomText, {.03,.18,.94,.08}, "CURRENT", {}, 44, "#58e38f", true, 0},
        {StageElement::Kind::CurrentSlide, {.03,.28,.94,.32}, {}, {}, 96, "#ffffff", true, 1},
        {StageElement::Kind::CustomText, {.03,.71,.94,.08}, "NEXT", {}, 44, "#f5c252", true, 0},
        {StageElement::Kind::NextSlide, {.03,.79,.94,.17}, {}, {}, 64, "#f5c252", false, 1}
    };
    return layout;
}
bool StageLayout::validate(QString *error) const {
    if (error) error->clear();
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (id.isEmpty() || id.size() > 64 || name.trimmed().isEmpty() || name.size() > 100 || !QColor(background).isValid()
        || elements.isEmpty() || elements.size() > 64)
        return fail("A stage layout needs a name, valid background, and 1–64 elements.");
    for (const auto &element : elements) {
        const auto r = element.rect;
        if (int(element.kind) < 0 || int(element.kind) > 6 || !qIsFinite(r.x()) || !qIsFinite(r.y())
            || !qIsFinite(r.width()) || !qIsFinite(r.height()) || r.x() < 0 || r.y() < 0
            || r.width() < .01 || r.height() < .01 || r.right() > 1.000001 || r.bottom() > 1.000001
            || element.text.size() > 4000 || element.fontFamily.size() > 200 || element.fontSize < 8 || element.fontSize > 500
            || !QColor(element.color).isValid() || element.alignment < 0 || element.alignment > 2)
            return fail("Keep each element inside the canvas with a valid font (8–500 px), color, alignment, and text up to 4000 characters.");
    }
    return true;
}
QJsonObject StageLayout::toJson() const {
    QJsonArray items;
    for (const auto &e : elements) items.append(QJsonObject{{"kind", int(e.kind)}, {"x", e.rect.x()}, {"y", e.rect.y()},
        {"w", e.rect.width()}, {"h", e.rect.height()}, {"text", e.text}, {"font", e.fontFamily}, {"size", e.fontSize},
        {"color", e.color}, {"bold", e.bold}, {"align", e.alignment}});
    return {{"id", id}, {"name", name}, {"background", background}, {"elements", items}};
}
bool StageLayout::fromJson(const QJsonObject &json, StageLayout *result, QString *error) {
    const auto fail = [error] { if (error) *error = "Invalid saved stage layout."; return false; };
    if (!json["id"].isString() || !json["name"].isString() || !json["background"].isString() || !json["elements"].isArray()) return fail();
    StageLayout parsed; parsed.id = json["id"].toString(); parsed.name = json["name"].toString(); parsed.background = json["background"].toString();
    if (json["elements"].toArray().size() > 64) return fail();
    for (const auto &entry : json["elements"].toArray()) {
        if (!entry.isObject()) return fail();
        const auto object = entry.toObject();
        for (const auto &key : {"kind", "size", "align"}) if (!object[key].isDouble() || object[key].toDouble() != object[key].toInt(-1)) return fail();
        for (const auto &key : {"x", "y", "w", "h"}) if (!object[key].isDouble()) return fail();
        for (const auto &key : {"text", "font", "color"}) if (!object[key].isString()) return fail();
        if (!object["bold"].isBool()) return fail();
        parsed.elements.append({StageElement::Kind(object["kind"].toInt()),
            {object["x"].toDouble(), object["y"].toDouble(), object["w"].toDouble(), object["h"].toDouble()},
            object["text"].toString(), object["font"].toString(), object["size"].toInt(), object["color"].toString(),
            object["bold"].toBool(), object["align"].toInt()});
    }
    if (!parsed.validate(error)) return false;
    *result = parsed; return true;
}
QString stageElementText(const StageElement &element, const LivePresentation &state, const StageRuntime &runtime) {
    const auto slide = [](const QString &text, const SlideMedia &media) {
        const QString label = media.kind == MediaKind::None ? QString() : media.name;
        return text.isEmpty() ? label : label.isEmpty() ? text : label + QLatin1Char('\n') + text;
    };
    switch (element.kind) {
    case StageElement::Kind::CurrentSlide: return slide(state.currentText(), state.currentMedia());
    case StageElement::Kind::NextSlide: return slide(state.nextText(), state.nextMedia());
    case StageElement::Kind::Title: return state.title;
    case StageElement::Kind::Clock: return QDateTime::currentDateTime().toString("h:mm:ss AP");
    case StageElement::Kind::Countdown: {
        const auto seconds = qMax<qint64>(0, (runtime.remainingMs + 999) / 1000);
        return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    case StageElement::Kind::Message: return runtime.message;
    case StageElement::Kind::CustomText: return element.text;
    }
    return {};
}
void paintStageLayout(QPainter &p, const QRectF &canvas, const StageLayout &layout, const LivePresentation &state, const StageRuntime &runtime) {
    p.save(); p.setClipRect(canvas, Qt::IntersectClip); p.fillRect(canvas, QColor(layout.background));
    const qreal scale = canvas.height() / 1080.;
    for (const auto &e : layout.elements) {
        const QRectF r(canvas.x() + e.rect.x() * canvas.width(), canvas.y() + e.rect.y() * canvas.height(),
                       e.rect.width() * canvas.width(), e.rect.height() * canvas.height());
        const auto text = stageElementText(e, state, runtime);
        QFont font(e.fontFamily); font.setBold(e.bold);
        const int flags = Qt::TextWordWrap | Qt::AlignVCenter | (e.alignment == 0 ? Qt::AlignLeft : e.alignment == 2 ? Qt::AlignRight : Qt::AlignHCenter);
        int low = qMax(1, qRound(8 * scale));
        int high = qMax(low, qRound(e.fontSize * scale));
        int size = low;
        while (low <= high) {
            const int candidate = low + (high - low) / 2;
            font.setPixelSize(candidate);
            const auto bounds = QFontMetricsF(font).boundingRect(r, flags, text);
            if (bounds.width() <= r.width() && bounds.height() <= r.height()) { size = candidate; low = candidate + 1; }
            else high = candidate - 1;
        }
        font.setPixelSize(size); p.setFont(font); p.setPen(QColor(e.color));
        p.save(); p.setClipRect(r, Qt::IntersectClip); p.drawText(r, flags, text); p.restore();
    }
    p.restore();
}
