#pragma once

#include "../data/LibraryRepository.h"
#include "PlanningCenterClient.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

// One stdio client; modern requests are stateless, legacy initialization is
// scoped to this instance. No presentation/output-control API is exposed.
class McpServer {
public:
    explicit McpServer(LibraryRepository &library, PlanningCenterClient::Fetch fetch = {})
        : library_(library), planningCenter_(std::move(fetch)) {}
    static QJsonArray extraToolDefinitions();
    static QJsonArray libraryToolDefinitions();
    static QString presentationRevision(const Song &song);
    QByteArray handleMessage(const QByteArray &message);
    static constexpr qsizetype maximumMessageBytes = 8 * 1024 * 1024;

private:
    LibraryRepository &library_;
    PlanningCenterClient planningCenter_;
    QJsonObject callCreationTool(const QString &name, const QJsonObject &arguments);
    QJsonObject callLibraryTool(const QString &name, const QJsonObject &arguments);
    bool initialized_ = false;
    bool ready_ = false;
    QJsonObject callTool(const QString &name, const QJsonObject &arguments,
                        const QMap<int, SongStructure> &importedStructures = {});
    QJsonObject serviceData(int id, QString *error, int offset, int limit) const;
};
