#include "PresentationLook.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QRectF PresentationLook::textRect() const {
    return QRectF(marginLeft + padding, marginTop + padding,
        1920 - marginLeft - marginRight - 2 * padding, 1080 - marginTop - marginBottom - 2 * padding);
}

bool PresentationLook::validate(QString *error) const {
    if (error) error->clear();
    static const QRegularExpression color(QStringLiteral("^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$"));
    const bool validPath = backgroundPath.isEmpty() || (!QDir::isAbsolutePath(backgroundPath)
        && backgroundPath.startsWith(QStringLiteral("backgrounds/")) && backgroundPath.size() > 12
        && QDir::cleanPath(backgroundPath) == backgroundPath && !backgroundPath.endsWith(QLatin1Char('/'))
        && !backgroundPath.contains(QLatin1Char('\\')) && !backgroundPath.contains(QChar::Null));
    const bool valid = fontFamily.size() <= 256 && !fontFamily.contains(QChar::Null)
        && fontSize >= 8 && fontSize <= 512 && fontWeight >= 100 && fontWeight <= 900
        && color.match(textColor).hasMatch() && color.match(backgroundColor).hasMatch()
        && horizontalAlignment >= 0 && horizontalAlignment <= 2 && verticalAlignment >= 0 && verticalAlignment <= 2
        && marginLeft >= 0 && marginRight >= 0 && marginTop >= 0 && marginBottom >= 0 && padding >= 0
        && marginLeft <= 1920 && marginRight <= 1920 && marginTop <= 1080 && marginBottom <= 1080 && padding <= 1080
        && textRect().width() > 0 && textRect().height() > 0
        && int(background) >= 0 && int(background) <= 2 && validPath
        && (background != Background::Image || !backgroundPath.isEmpty());
    if (!valid && error) *error = QStringLiteral("Use a supported font size, weight, alignment, colors, and a text box inside the canvas. Image backgrounds must come from the Background Library.");
    return valid;
}

QString PresentationLook::toJson() const {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"fontFamily", fontFamily}, {"fontSize", fontSize}, {"fontWeight", fontWeight},
        {"italic", italic}, {"underline", underline}, {"strikeOut", strikeOut}, {"textColor", textColor},
        {"horizontalAlignment", horizontalAlignment}, {"verticalAlignment", verticalAlignment},
        {"marginLeft", marginLeft}, {"marginRight", marginRight}, {"marginTop", marginTop}, {"marginBottom", marginBottom},
        {"padding", padding}, {"background", int(background)}, {"backgroundColor", backgroundColor}, {"backgroundPath", backgroundPath}
    }).toJson(QJsonDocument::Compact));
}

bool PresentationLook::fromJson(const QString &json, PresentationLook *look, QString *error) {
    if (error) error->clear();
    *look = {};
    if (json.size() > 16384) { if (error) *error = QStringLiteral("Look metadata exceeds the size limit."); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    const auto object = document.object();
    const auto invalid = [error] { if (error) *error = QStringLiteral("A saved Look contains invalid metadata."); return false; };
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto integer = [&object](const QString &key, int *target) {
        const auto value = object.value(key);
        if (!value.isDouble() || value.toInt(-9999) != value.toDouble()) return false;
        *target = value.toInt();
        return true;
    };
    for (const auto &key : {QStringLiteral("fontFamily"), QStringLiteral("textColor"), QStringLiteral("backgroundColor"), QStringLiteral("backgroundPath")})
        if (!object.value(key).isString()) return invalid();
    for (const auto &key : {QStringLiteral("italic"), QStringLiteral("underline"), QStringLiteral("strikeOut")})
        if (!object.value(key).isBool()) return invalid();
    int background = 0;
    if (!integer("fontSize", &look->fontSize) || !integer("fontWeight", &look->fontWeight)
        || !integer("horizontalAlignment", &look->horizontalAlignment) || !integer("verticalAlignment", &look->verticalAlignment)
        || !integer("marginLeft", &look->marginLeft) || !integer("marginRight", &look->marginRight)
        || !integer("marginTop", &look->marginTop) || !integer("marginBottom", &look->marginBottom)
        || !integer("padding", &look->padding) || !integer("background", &background)) return invalid();
    look->fontFamily = object.value("fontFamily").toString();
    look->textColor = object.value("textColor").toString();
    look->backgroundColor = object.value("backgroundColor").toString();
    look->backgroundPath = object.value("backgroundPath").toString();
    look->italic = object.value("italic").toBool();
    look->underline = object.value("underline").toBool();
    look->strikeOut = object.value("strikeOut").toBool();
    look->background = Background(background);
    return look->validate(error);
}

bool PresentationAppearance::validate(int count) const {
    if (lookId < 0 || (!slideLookIds.isEmpty() && slideLookIds.size() != count)
        || (!richTextOverrides.isEmpty() && richTextOverrides.size() != count)
        || (!fitText.isEmpty() && fitText.size() != count)) return false;
    for (int id : slideLookIds) if (id < 0) return false;
    return true;
}
