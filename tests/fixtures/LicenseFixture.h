#pragma once
#include "../../src/licensing/LicenseCrypto.h"
#include "../../src/licensing/LicenseManager.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSysInfo>
#include <functional>
// Ephemeral test-only authority: never a developer or production license.
class MemoryLicenseStore : public LicenseStore {
  public:
    QByteArray state;
    bool writable = true;
    bool read(QByteArray *result, QString *) override {
        *result = state;
        return true;
    }
    bool write(const QByteArray &value, QString *error) override {
        if (!writable) {
            *error = "Fixture storage unavailable";
            return false;
        }
        state = value;
        return true;
    }
};
inline QByteArray fixtureLease(const QByteArray &authority, const QString &installation,
                               const QString &license, qint64 issued, qint64 expiry,
                               QString status = "active") {
    const QJsonObject claims{{"v", 1},
                             {"aud", "worshipcue-live"},
                             {"status", status},
                             {"licenseId", license},
                             {"installationId", installation},
                             {"iat", double(issued)},
                             {"exp", double(expiry)}};
    auto payload = LicenseCrypto::encode(QJsonDocument(claims).toJson(QJsonDocument::Compact));
    return payload + '.' + LicenseCrypto::encode(LicenseCrypto::sign(authority, payload));
}
inline std::unique_ptr<LicenseManager>
fixtureLicense(bool licensed = true, std::function<void(QJsonObject &)> mutate = {},
               MemoryLicenseStore **storeObserver = nullptr) {
    using namespace LicenseCrypto;
    auto authority = random(32), seed = random(32);
    const auto now = QDateTime::currentSecsSinceEpoch();
    auto store = std::make_unique<MemoryLicenseStore>();
    if (storeObserver) *storeObserver = store.get();
    if (licensed) {
        const auto lease =
            fixtureLease(authority, QString::fromLatin1(installationId(publicKey(seed))), "fixture",
                         now, now + 86400);
        store->state =
            QJsonDocument(
                QJsonObject{
                    {"v", 1},
                    {"seed", QString::fromLatin1(encode(seed))},
                    {"lease", QString::fromLatin1(lease)},
                    {"licenseId", "fixture"},
                    {"machine", QString::fromLatin1(encode(QCryptographicHash::hash(
                                    QSysInfo::machineUniqueId(), QCryptographicHash::Sha256)))},
                    {"highWater", double(now)},
                    {"denied", false}})
                .toJson(QJsonDocument::Compact);
    }
    if (mutate) {
        auto object = QJsonDocument::fromJson(store->state).object();
        mutate(object);
        store->state = QJsonDocument(object).toJson(QJsonDocument::Compact);
    }
    auto manager = std::make_unique<LicenseManager>(
        nullptr, LicenseConfiguration{QUrl("https://127.0.0.1:1"), publicKey(authority)},
        std::move(store));
    manager->start();
    return manager;
}
