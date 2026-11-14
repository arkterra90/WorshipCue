#pragma once

#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QVector>

struct PresentationLook {
    enum class Background { Presentation, Color, Image };
    QString fontFamily;
    int fontSize = 112;
    int fontWeight = 700;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    QString textColor = QStringLiteral("#ffffff");
    // 0 = left/top, 1 = center, 2 = right/bottom.
    int horizontalAlignment = 1;
    int verticalAlignment = 1;
    int marginLeft = 154;
    int marginRight = 154;
    int marginTop = 130;
    int marginBottom = 130;
    int padding = 0;
    Background background = Background::Presentation;
    QString backgroundColor = QStringLiteral("#000000");
    QString backgroundPath;

    QRectF textRect() const;
    bool validate(QString *error = nullptr) const;
    QString toJson() const;
    static bool fromJson(const QString &json, PresentationLook *look, QString *error);
};

struct SavedLook {
    int id = 0;
    QString name;
    PresentationLook settings;
};

struct SlideLook {
    int id = 0;
    PresentationLook settings;
    bool richTextOverrides = false;
    bool fitText = false;
    // Resolved by the GUI at the preview/playback boundary.
    QString backgroundPath;
    bool operator==(const SlideLook &other) const {
        return id == other.id && richTextOverrides == other.richTextOverrides && fitText == other.fitText
            && backgroundPath == other.backgroundPath && (id == 0 || settings.toJson() == other.settings.toJson());
    }
    bool operator!=(const SlideLook &other) const { return !(*this == other); }
};
Q_DECLARE_METATYPE(SlideLook)

struct PresentationAppearance {
    int lookId = 0;
    QVector<int> slideLookIds;
    QVector<bool> richTextOverrides;
    QVector<bool> fitText;
    bool validate(int count) const;
    bool operator==(const PresentationAppearance &other) const {
        return lookId == other.lookId && slideLookIds == other.slideLookIds
            && richTextOverrides == other.richTextOverrides && fitText == other.fitText;
    }
    bool operator!=(const PresentationAppearance &other) const { return !(*this == other); }
};
