#pragma once
#include "../core/PresentationController.h"
#include <QJsonObject>
#include <QRectF>
#include <QVector>
class QPainter;
struct StageElement {
    enum class Kind { CurrentSlide, NextSlide, Title, Clock, Countdown, Message, CustomText };
    Kind kind = Kind::CurrentSlide;
    QRectF rect{.05, .15, .9, .45};
    QString text;
    QString fontFamily;
    int fontSize = 80;
    QString color = QStringLiteral("#ffffff");
    bool bold = false;
    int alignment = 1;
};
struct StageRuntime {
    QString message;
    qint64 remainingMs = 300000;
    bool running = false;
};
struct StageLayout {
    QString id = QStringLiteral("standard");
    QString name = QStringLiteral("Standard Stage");
    QString background = QStringLiteral("#0c0f15");
    QVector<StageElement> elements;
    bool validate(QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, StageLayout *, QString *error = nullptr);
    static StageLayout standard();
    static QString elementName(StageElement::Kind kind);
};
QString stageElementText(const StageElement &element, const LivePresentation &state, const StageRuntime &runtime);
void paintStageLayout(QPainter &painter, const QRectF &canvas, const StageLayout &layout,
                      const LivePresentation &state, const StageRuntime &runtime);
