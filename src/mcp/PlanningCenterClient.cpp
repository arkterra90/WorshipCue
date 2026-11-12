#include "PlanningCenterClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

namespace {
const QString root = QStringLiteral("https://api.planningcenteronline.com/services/v2/");
QUrl collection(const QString &path, int offset, int limit) {
    QUrl url(root + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    query.addQueryItem(QStringLiteral("per_page"), QString::number(limit));
    url.setQuery(query);
    return url;
}
}
bool PlanningCenterClient::validId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[1-9][0-9]{0,19}$"));
    return pattern.match(id).hasMatch();
}
bool PlanningCenterClient::trustedUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host() == QStringLiteral("api.planningcenteronline.com")
        && url.port(443) == 443 && url.userInfo().isEmpty() && url.fragment().isEmpty()
        && url.path().startsWith(QStringLiteral("/services/v2/"))
        && !url.path().contains(QStringLiteral(".."));
}
QJsonObject PlanningCenterClient::get(const QUrl &url, QString *error) const {
    if (!trustedUrl(url)) { *error = QStringLiteral("Planning Center returned an untrusted API URL."); return {}; }
    if (fetch_) return fetch_(url, error);
    const auto id = qgetenv("PLANNING_CENTER_APPLICATION_ID");
    const auto secret = qgetenv("PLANNING_CENTER_SECRET");
    if (id.isEmpty() || secret.isEmpty()) {
        *error = QStringLiteral("Configure PLANNING_CENTER_APPLICATION_ID and PLANNING_CENTER_SECRET in the MCP client's server environment using a Planning Center Personal Access Token.");
        return {};
    }
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Basic " + (id + ':' + secret).toBase64());
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-PCO-API-Version", "2018-11-01");
    request.setRawHeader("User-Agent", "WorshipCue/0.3.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = network.get(request);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QByteArray body;
    bool oversized = false;
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
        body += reply->readAll();
        if (body.size() > 8 * 1024 * 1024) { oversized = true; reply->abort(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(20000);
    loop.exec();
    body += reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (oversized || body.size() > 8 * 1024 * 1024) *error = QStringLiteral("Planning Center response exceeds 8 MiB.");
    else if (status == 401 || status == 403) *error = QStringLiteral("Planning Center denied access. Check the Personal Access Token and Services permissions.");
    else if (status == 429) *error = QStringLiteral("Planning Center rate limit reached. Retry later.");
    else if (status != 200 || reply->error() != QNetworkReply::NoError)
        *error = QStringLiteral("Planning Center request failed (HTTP %1). Check the connection and plan IDs, then retry.").arg(status);
    else {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(body, &parse);
        if (parse.error == QJsonParseError::NoError && doc.isObject() && !doc.object().contains(QStringLiteral("errors"))) return doc.object();
        *error = QStringLiteral("Planning Center returned invalid API data.");
    }
    return {};
}
QJsonObject PlanningCenterClient::serviceTypes(int offset, int limit, QString *error) const {
    return get(collection(QStringLiteral("service_types"), offset, limit), error);
}
QJsonObject PlanningCenterClient::plans(const QString &serviceType, int offset, int limit, QString *error) const {
    auto url = collection(QStringLiteral("service_types/%1/plans").arg(serviceType), offset, limit);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("order"), QStringLiteral("-sort_date"));
    url.setQuery(query);
    return get(url, error);
}
QJsonObject PlanningCenterClient::plan(const QString &serviceType, const QString &planId, QString *error) const {
    QString resolvedType = serviceType;
    QString path = resolvedType.isEmpty() ? QStringLiteral("plans/%1").arg(planId)
        : QStringLiteral("service_types/%1/plans/%2").arg(resolvedType, planId);
    const auto plan = get(QUrl(root + path), error);
    if (!error->isEmpty()) return {};
    if (!plan.value(QStringLiteral("data")).isObject()) { *error = QStringLiteral("Planning Center plan data is missing."); return {}; }
    if (resolvedType.isEmpty()) {
        resolvedType = plan.value(QStringLiteral("data")).toObject().value(QStringLiteral("relationships")).toObject()
            .value(QStringLiteral("service_type")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toString();
        if (!validId(resolvedType)) { *error = QStringLiteral("Planning Center did not return this plan's service type. Supply service_type_id explicitly."); return {}; }
        path = QStringLiteral("service_types/%1/plans/%2").arg(resolvedType, planId);
    }
    auto url = collection(path + QStringLiteral("/items"), 0, 100);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("include"), QStringLiteral("song,arrangement"));
    url.setQuery(query);
    QJsonArray items;
    QJsonArray included;
    QSet<QString> pages;
    qsizetype totalBytes = 0;
    for (int page = 0; page < 6; ++page) {
        if (pages.contains(url.toString())) { *error = QStringLiteral("Planning Center pagination repeated a page."); return {}; }
        pages.insert(url.toString());
        const auto data = get(url, error);
        if (!error->isEmpty()) return {};
        totalBytes += QJsonDocument(data).toJson(QJsonDocument::Compact).size();
        if (!data.value(QStringLiteral("data")).isArray() || totalBytes > 32 * 1024 * 1024) {
            *error = QStringLiteral("Planning Center items are invalid or exceed the import size limit."); return {};
        }
        for (const auto &item : data.value(QStringLiteral("data")).toArray()) items.append(item);
        for (const auto &entry : data.value(QStringLiteral("included")).toArray()) included.append(entry);
        if (items.size() > 500) { *error = QStringLiteral("Plans can contain at most 500 items for import."); return {}; }
        const auto next = data.value(QStringLiteral("links")).toObject().value(QStringLiteral("next"));
        if (next.isNull() || next.isUndefined()) {
            // Service positions are distinct timelines; sequence orders items within each.
            auto rank = [](const QJsonValue &item) {
                const auto position = item.toObject().value(QStringLiteral("attributes")).toObject().value(QStringLiteral("service_position")).toString();
                return position == QStringLiteral("pre") ? 0 : position == QStringLiteral("post") ? 2 : 1;
            };
            QVector<QJsonValue> sorted;
            for (const auto &item : items) sorted.append(item);
            std::stable_sort(sorted.begin(), sorted.end(), [&](const auto &a, const auto &b) {
                if (rank(a) != rank(b)) return rank(a) < rank(b);
                return a.toObject()[QStringLiteral("attributes")].toObject()[QStringLiteral("sequence")].toInt()
                    < b.toObject()[QStringLiteral("attributes")].toObject()[QStringLiteral("sequence")].toInt();
            });
            items = {};
            for (const auto &item : sorted) items.append(item);
            return {{QStringLiteral("service_type_id"), resolvedType}, {QStringLiteral("plan_id"), planId}, {QStringLiteral("plan"), plan.value(QStringLiteral("data"))}, {QStringLiteral("items"), items}, {QStringLiteral("included"), included}};
        }
        const QUrl candidate = url.resolved(QUrl(next.toString()));
        if (!next.isString() || !trustedUrl(candidate) || candidate.path() != url.path()) {
            *error = QStringLiteral("Planning Center returned invalid item pagination."); return {};
        }
        url = candidate;
    }
    *error = QStringLiteral("Planning Center plan exceeds the pagination limit.");
    return {};
}
