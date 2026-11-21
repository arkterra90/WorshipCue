#pragma once
#include "LicenseStore.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>

struct LicenseConfiguration {
    QUrl endpoint;
    QByteArray publicKey;
    static LicenseConfiguration production();
};
class LicenseManager : public QObject {
    Q_OBJECT
  public:
    explicit LicenseManager(QObject *parent = nullptr,
                            LicenseConfiguration configuration = LicenseConfiguration::production(),
                            std::unique_ptr<LicenseStore> store = nativeLicenseStore());
    void start();
    bool liveAllowed() const;
    bool busy() const { return busy_; }
    bool configured() const;
    bool retryingCacheWrite() const { return storageFailure_.isValid(); }
    QString status() const;
    QString licenseId() const { return licenseId_; }
    QString installationId() const;
    QDateTime expiresAt() const {
        return QDateTime::fromSecsSinceEpoch(retryingCacheWrite() ? persistedExpiry_ : expiry_, Qt::UTC);
    }
    QJsonArray devices() const { return devices_; }
    void activate(const QString &code, const QString &label);
    void verifyOnline();
    void deactivate();
    void refreshDevices();
    void releaseDevice(const QString &id);
    static bool validateLease(const QByteArray &lease, const QByteArray &publicKey,
                              const QString &installationId, const QString &licenseId, qint64 now,
                              qint64 *expiry = nullptr);
  signals:
    void changed();

  private slots:
    void tick();

  private:
    LicenseConfiguration config_;
    std::unique_ptr<LicenseStore> store_;
    QNetworkAccessManager network_;
    QTimer timer_;
    QByteArray seed_, lease_, persistedLease_, machine_;
    QString licenseId_,
        status_ = QStringLiteral("Editing only — activate a license to present live.");
    QString persistedLicenseId_, storageError_;
    QJsonArray devices_;
    qint64 expiry_ = 0, persistedExpiry_ = 0, highWater_ = 0, lastAttempt_ = 0, clockBase_ = 0;
    QElapsedTimer monotonic_;
    QElapsedTimer storageFailure_;
    qint64 retryAfterSeconds_ = 86400;
    bool started_ = false, busy_ = false, storageReady_ = false, denied_ = false;
    bool identityPersisted_ = false;
    bool save();
    void request(const QString &path, QJsonObject fields = {});
    qint64 now() const;
};
