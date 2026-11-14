#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>

enum class OutputId { Audience1, Audience2, Stage1, Stage2 };
inline constexpr OutputId outputIds[] = {OutputId::Audience1, OutputId::Audience2, OutputId::Stage1, OutputId::Stage2};
inline bool isStageOutput(OutputId id) { return id == OutputId::Stage1 || id == OutputId::Stage2; }
QString outputName(OutputId id);

struct OutputConfiguration {
    enum class Scaling { Fit, Fill, Stretch };
    QString target = QStringLiteral("off");
    QSize canvas{1920, 1080};
    int aspectWidth = 16;
    int aspectHeight = 9;
    int rotation = 0;
    Scaling scaling = Scaling::Fit;
    // Empty means retain the OS desktop mode; never invent hardware modes.
    QString signalMode;
    QString stageLayout = QStringLiteral("standard");
    bool validate(QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, OutputConfiguration *result, QString *error = nullptr);
};

// The content rectangle before rotation; Fit letterboxes, Fill crops, Stretch fills.
QRectF outputContentRect(const QRectF &destination, qreal aspect, OutputConfiguration::Scaling scaling);
