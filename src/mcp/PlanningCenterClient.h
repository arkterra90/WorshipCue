#pragma once

#include <QJsonObject>
#include <QUrl>
#include <functional>

// Read-only Services API. Credentials stay in the server process environment.
class PlanningCenterClient {
public:
    using Fetch = std::function<QJsonObject(const QUrl &, QString *)>;
    explicit PlanningCenterClient(Fetch fetch = {}) : fetch_(std::move(fetch)) {}
    QJsonObject serviceTypes(int offset, int limit, QString *error) const;
    QJsonObject plans(const QString &serviceType, int offset, int limit, QString *error) const;
    QJsonObject plan(const QString &serviceType, const QString &planId, QString *error) const;
    static bool validId(const QString &id);
    static bool trustedUrl(const QUrl &url);
private:
    Fetch fetch_;
    QJsonObject get(const QUrl &url, QString *error) const;
};
