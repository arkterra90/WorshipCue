#include "LicenseManager.h"
#include "LicenseBuildConfiguration.h"
#include "LicenseCrypto.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <algorithm>
using namespace LicenseCrypto;
namespace {
constexpr qint64 storageGraceMs = 5 * 60 * 1000;
}
LicenseConfiguration LicenseConfiguration::production() {
    return {QUrl(QStringLiteral(WORSHIPCUE_LICENSE_URL)),
            decode(QByteArray(WORSHIPCUE_LICENSE_PUBLIC_KEY))};
}
LicenseManager::LicenseManager(QObject *parent, LicenseConfiguration configuration,
                               std::unique_ptr<LicenseStore> store)
    : QObject(parent), config_(std::move(configuration)), store_(std::move(store)), network_(this) {
    timer_.setInterval(30000);
    connect(&timer_, &QTimer::timeout, this, &LicenseManager::tick);
}
bool LicenseManager::configured() const {
    return config_.endpoint.scheme() == "https" && !config_.endpoint.host().isEmpty() &&
           config_.endpoint.userInfo().isEmpty() && config_.endpoint.query().isEmpty() &&
           config_.endpoint.fragment().isEmpty() && config_.publicKey.size() == 32;
}
qint64 LicenseManager::now() const {
    return std::max(QDateTime::currentSecsSinceEpoch(),
                    clockBase_ + (monotonic_.isValid() ? monotonic_.elapsed() / 1000 : 0));
}
QString LicenseManager::installationId() const {
    return QString::fromLatin1(LicenseCrypto::installationId(publicKey(seed_)));
}
bool LicenseManager::validateLease(const QByteArray &lease, const QByteArray &key,
                                   const QString &installation, const QString &license, qint64 now,
                                   qint64 *expiry) {
    if (lease.size() > 8192)
        return false;
    const auto parts = lease.split('.');
    if (parts.size() != 2 || !verify(key, parts[0], decode(parts[1])))
        return false;
    const auto object = QJsonDocument::fromJson(decode(parts[0])).object();
    const auto iat = object["iat"].toVariant().toLongLong(),
               exp = object["exp"].toVariant().toLongLong();
    const bool valid = object["v"] == 1 && object["aud"] == "worshipcue-live" &&
                       object["status"] == "active" && !license.isEmpty() &&
                       object["licenseId"].toString() == license &&
                       object["installationId"].toString() == installation && iat > 0 &&
                       iat <= now + 300 && exp > now && exp > iat && exp - iat <= 7 * 86400;
    if (valid && expiry)
        *expiry = exp;
    return valid;
}
bool LicenseManager::liveAllowed() const {
    const auto wall = QDateTime::currentSecsSinceEpoch();
    if (!started_ || !storageReady_ || !identityPersisted_ || denied_ || !configured() || wall + 300 < highWater_ ||
        wall + 300 < clockBase_ + (monotonic_.isValid() ? monotonic_.elapsed() / 1000 : 0)) return false;
    if (storageFailure_.isValid() && storageFailure_.elapsed() >= storageGraceMs) return false;
    const bool retrying = retryingCacheWrite();
    return validateLease(retrying ? persistedLease_ : lease_, config_.publicKey, installationId(),
                         retrying ? persistedLicenseId_ : licenseId_, now());
}

QString LicenseManager::status() const {
    if (storageError_.isEmpty()) return status_;
    return status_ + (liveAllowed() ? QStringLiteral("\nLicense cache temporarily unavailable. Using the saved authorization while retrying. ")
                                   : QStringLiteral("\nLicense cache could not be saved. Retrying. ")) + storageError_;
}
bool LicenseManager::save() {
    const QJsonObject state{{"v", 1},
                            {"seed", QString::fromLatin1(encode(seed_))},
                            {"lease", QString::fromLatin1(lease_)},
                            {"licenseId", licenseId_},
                            {"machine", QString::fromLatin1(machine_)},
                            {"highWater", double(highWater_)},
                            {"denied", denied_}};
    QString error;
    if (!store_->write(QJsonDocument(state).toJson(QJsonDocument::Compact), &error)) {
        if (!storageFailure_.isValid()) storageFailure_.start();
        storageError_ = error.isEmpty() ? QStringLiteral("Protected storage is unavailable.") : error;
        return false;
    }
    identityPersisted_ = true;
    persistedLease_ = lease_;
    persistedLicenseId_ = licenseId_;
    persistedExpiry_ = expiry_;
    storageFailure_.invalidate();
    storageError_.clear();
    return true;
}
void LicenseManager::start() {
    if (started_)
        return;
    started_ = true;
    clockBase_ = QDateTime::currentSecsSinceEpoch();
    monotonic_.start();
    QByteArray data;
    QString error;
    if (!store_->read(&data, &error)) {
        status_ = error;
        emit changed();
        return;
    }
    const auto machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) {
        status_ = "This machine has no stable installation binding. Editing "
                  "remains available.";
        emit changed();
        return;
    }
    machine_ = encode(QCryptographicHash::hash(machineId, QCryptographicHash::Sha256));
    if (!data.isEmpty()) {
        const auto object = QJsonDocument::fromJson(data).object();
        seed_ = decode(object["seed"].toString().toLatin1());
        if (data.size() > 65536 || object["v"] != 1 || seed_.size() != 32 ||
            object["machine"].toString().toLatin1() != machine_) {
            status_ = "Protected licensing state is invalid or belongs to another "
                      "machine. Contact support to recover this installation.";
            emit changed();
            return;
        }
        lease_ = object["lease"].toString().toLatin1();
        licenseId_ = object["licenseId"].toString();
        highWater_ = object["highWater"].toVariant().toLongLong();
        denied_ = object["denied"].toBool();
        identityPersisted_ = true;
        persistedLease_ = lease_;
        persistedLicenseId_ = licenseId_;
        validateLease(persistedLease_, config_.publicKey, installationId(), persistedLicenseId_, now(), &persistedExpiry_);
        expiry_ = persistedExpiry_;
    } else
        seed_ = random(32);
    if (seed_.size() != 32) {
        status_ = "Could not create a secure installation identity.";
        emit changed();
        return;
    }
    storageReady_ = true;
    highWater_ = std::max(highWater_, clockBase_);
    save();
    if (!configured())
        status_ = "Editing only — the licensing service is not configured in this build.";
    else if (liveAllowed()) {
        status_ = "Licensed — offline authorization valid until " +
                  expiresAt().toLocalTime().toString(Qt::ISODate);
    } else
        status_ = "Editing only — activate or verify your license. Check the "
                  "system clock if it changed.";
    timer_.start();
    emit changed();
    // Startup verification checks revocations, while a valid lease allows work
    // during outages.
    if (configured() && identityPersisted_ && !licenseId_.isEmpty())
        QTimer::singleShot(0, this, &LicenseManager::verifyOnline);
}
void LicenseManager::tick() {
    if (!storageReady_)
        return;
    highWater_ = std::max(highWater_, now());
    const bool recovering = storageFailure_.isValid();
    const bool saved = save();
    if (!liveAllowed() && !denied_ && configured())
        status_ = "Editing only — live authorization has expired or is invalid. "
                  "Connect to verify your license.";
    else if (recovering && saved && liveAllowed())
        status_ = "Licensed — offline authorization valid until " + expiresAt().toLocalTime().toString(Qt::ISODate);
    emit changed();
    if (configured() && identityPersisted_ && !licenseId_.isEmpty() && now() - lastAttempt_ >= retryAfterSeconds_)
        verifyOnline();
}
void LicenseManager::activate(const QString &code, const QString &label) {
    request("/v1/activate", {{"code", code.trimmed()}, {"label", label.trimmed().left(80)}});
}
void LicenseManager::verifyOnline() {
    if (!licenseId_.isEmpty())
        request("/v1/verify");
}
void LicenseManager::deactivate() {
    if (!licenseId_.isEmpty())
        request("/v1/deactivate");
}
void LicenseManager::refreshDevices() {
    if (!licenseId_.isEmpty())
        request("/v1/devices");
}
void LicenseManager::releaseDevice(const QString &id) {
    if (id == installationId())
        deactivate();
    else
        request("/v1/release", {{"target", id}});
}
void LicenseManager::request(const QString &path, QJsonObject fields) {
    if (busy_)
        return;
    if (!started_)
        start();
    if (!identityPersisted_ || !configured()) {
        status_ = "Activation requires configured HTTPS licensing and protected OS "
                  "credential storage.";
        emit changed();
        return;
    }
    const auto timestamp = QDateTime::currentSecsSinceEpoch();
    fields["path"] = path;
    fields["nonce"] = QString::fromLatin1(encode(random(24)));
    fields["timestamp"] = double(timestamp);
    fields["publicKey"] = QString::fromLatin1(encode(publicKey(seed_)));
    fields["licenseId"] = licenseId_;
    const auto payload = encode(QJsonDocument(fields).toJson(QJsonDocument::Compact));
    QJsonObject body{{"payload", QString::fromLatin1(payload)},
                     {"signature", QString::fromLatin1(encode(sign(seed_, payload)))},
                     {"nonce", fields["nonce"]},
                     {"timestamp", fields["timestamp"]},
                     {"publicKey", fields["publicKey"]}};
    QUrl url = config_.endpoint;
    url.setPath(path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);
    auto *reply = network_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    busy_ = true;
    lastAttempt_ = now();
    status_ = "Contacting licensing service…";
    emit changed();
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 65536)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        busy_ = false;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto data = reply->readAll();
        const auto object =
            data.size() <= 65536 ? QJsonDocument::fromJson(data).object() : QJsonObject{};
        const auto error = object["error"].toString();
        if (http == 200 && reply->error() == QNetworkReply::NoError) {
            if (path == "/v1/deactivate") {
                lease_.clear();
                licenseId_.clear();
                expiry_ = 0;
                devices_ = {};
                denied_ = true;
                save();
                status_ = "Deactivated — editing remains available. Activate on "
                          "another machine to transfer.";
            } else if (path == "/v1/release") {
                status_ = "Device released. Its cached offline lease can remain valid "
                          "for up to seven days.";
                QTimer::singleShot(0, this, &LicenseManager::refreshDevices);
            } else {
                const auto candidate = object["lease"].toString().toLatin1();
                const auto decoded =
                    QJsonDocument::fromJson(decode(candidate.split('.').value(0))).object();
                const auto id = decoded["licenseId"].toString();
                qint64 exp = 0;
                if ((path == "/v1/activate" || id == licenseId_) &&
                    validateLease(candidate, config_.publicKey, installationId(), id,
                                  QDateTime::currentSecsSinceEpoch(), &exp)) {
                    lease_ = candidate;
                    licenseId_ = id;
                    expiry_ = exp;
                    denied_ = false;
                    clockBase_ = QDateTime::currentSecsSinceEpoch();
                    monotonic_.restart();
                    highWater_ = clockBase_;
                    devices_ = object["devices"].toArray();
                    save();
                    status_ = liveAllowed() ? "Licensed — offline authorization valid until " +
                                  expiresAt().toLocalTime().toString(Qt::ISODate)
                                           : QStringLiteral("Authorization received. Waiting to save it in protected storage.");
                } else
                    status_ = "The service returned invalid authorization. Existing "
                              "valid offline authorization is retained.";
            }
        } else if (http == 403 &&
                   (error == "license_revoked" || error == "installation_deactivated" ||
                    error == "invalid_license")) {
            // An invalid replacement code must not discard an existing valid license.
            if (path != "/v1/activate") {
                denied_ = true;
                lease_.clear();
                expiry_ = 0;
                save();
            }
            status_ = error == "license_revoked" ? "License revoked — live output disabled."
                      : error == "installation_deactivated"
                          ? "This installation was deactivated — live output disabled."
                          : "The license code is invalid.";
        } else if (error == "activation_limit")
            status_ = "All five installations are activated. Deactivate a device "
                      "from a licensed machine, then try again.";
        else if (error == "rate_limited")
            status_ = "Too many requests. Wait a minute and try again.";
        else
            status_ = liveAllowed() ? "Licensing service unavailable — using valid offline "
                                      "authorization."
                                    : "Licensing service unavailable — editing remains "
                                      "available. Check internet access and try again.";
        retryAfterSeconds_ = http == 200 ? 86400 : liveAllowed() ? 3600 : 300;
        reply->deleteLater();
        emit changed();
    });
}
