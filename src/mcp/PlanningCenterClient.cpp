#include "PlanningCenterClient.h"
#include "BuildInformation.h"

#include <QEventLoop>
#include <QElapsedTimer>
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
QNetworkRequest PlanningCenterClient::requestFor(const QUrl &url, QString *error) {
    error->clear();
    if (!trustedUrl(url)) { *error = QStringLiteral("Planning Center returned an untrusted API URL."); return {}; }
    const auto id = qgetenv("PLANNING_CENTER_APPLICATION_ID");
    const auto secret = qgetenv("PLANNING_CENTER_SECRET");
    const auto token = qgetenv("PLANNING_CENTER_ACCESS_TOKEN");
    const auto invalid = [](const QByteArray &value) {
        return value.size() > 16384 || value.contains('\r') || value.contains('\n') || value.contains('\0');
    };
    if (invalid(id) || invalid(secret) || invalid(token) || (token.isEmpty() && id.contains(':'))) {
        *error = QStringLiteral("Planning Center credentials have an invalid format. Check the server environment."); return {};
    }
    if (token.isEmpty() && (id.isEmpty() || secret.isEmpty())) {
        *error = QStringLiteral("Configure PLANNING_CENTER_APPLICATION_ID and PLANNING_CENTER_SECRET for your church's Personal Access Token, or PLANNING_CENTER_ACCESS_TOKEN for OAuth, in the MCP client's server environment.");
        return {};
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", token.isEmpty() ? "Basic " + (id + ':' + secret).toBase64() : "Bearer " + token);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-PCO-API-Version", "2018-11-01");
    request.setRawHeader("User-Agent", QByteArray("WorshipCue/") + BuildInformation::version().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    return request;
}
QJsonObject PlanningCenterClient::get(const QUrl &url, QString *error) const {
    error->clear();
    if (!trustedUrl(url)) { *error = QStringLiteral("Planning Center returned an untrusted API URL."); return {}; }
    if (fetch_) return fetch_(url, error);
    const auto request = requestFor(url, error);
    if (!error->isEmpty()) return {};
    QNetworkAccessManager network;
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
    else if (status == 401) *error = QStringLiteral("Planning Center denied authentication. Check the Personal Access Token, or renew the OAuth access token, then reconnect the MCP client.");
    else if (status == 403) *error = QStringLiteral("Planning Center denied Services access. Check the user's plan/song permissions and the OAuth services scope.");
    else if (status == 404) *error = QStringLiteral("Planning Center could not find this plan or service type. Check its IDs and your Services access.");
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
    if (!validId(planId) || (!resolvedType.isEmpty() && !validId(resolvedType))) {
        *error = QStringLiteral("Use numeric Planning Center plan and service type IDs."); return {};
    }
    if (resolvedType.isEmpty()) {
        // The documented Plan endpoints are scoped to a service type. Resolve
        // ordinary browser links with filtered collections rather than an
        // undocumented /services/v2/plans/<id> request.
        const auto types = serviceTypes(0, 100, error);
        if (!error->isEmpty()) return {};
        if (!types["data"].isArray()) { *error = QStringLiteral("Planning Center service types are invalid."); return {}; }
        QElapsedTimer resolutionClock;
        resolutionClock.start();
        const auto serviceTypes = types["data"].toArray();
        for (int index = 0; index < qMin(100, int(serviceTypes.size())); ++index) {
            const auto type = serviceTypes.at(index);
            const auto id = type.toObject()["id"].toString();
            if (!validId(id)) { *error = QStringLiteral("Planning Center service type IDs are invalid."); return {}; }
            if (resolutionClock.elapsed() > 45000) break;
            auto candidate = collection(QStringLiteral("service_types/%1/plans").arg(id), 0, 1);
            auto query = QUrlQuery(candidate);
            query.addQueryItem(QStringLiteral("where[id]"), planId);
            candidate.setQuery(query);
            const auto matches = get(candidate, error);
            if (!error->isEmpty()) return {};
            if (!matches["data"].isArray()) { *error = QStringLiteral("Planning Center plans are invalid."); return {}; }
            for (const auto &match : matches["data"].toArray())
                if (match.toObject()["id"].toString() == planId) { resolvedType = id; break; }
            if (!resolvedType.isEmpty()) break;
        }
        if (resolvedType.isEmpty()) {
            *error = QStringLiteral("Could not resolve this plan among the first 100 accessible service types. Use list_planning_center_service_types/list_planning_center_plans and supply service_type_id with plan_id."); return {};
        }
    }
    const auto path = QStringLiteral("service_types/%1/plans/%2").arg(resolvedType, planId);
    const auto plan = get(QUrl(root + path), error);
    if (!error->isEmpty()) return {};
    if (!plan["data"].isObject() || plan["data"].toObject()["id"].toString() != planId
        || plan["data"].toObject()["type"].toString() != "Plan") {
        *error = QStringLiteral("Planning Center returned missing or mismatched plan data."); return {};
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
