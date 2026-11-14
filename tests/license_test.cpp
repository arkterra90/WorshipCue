#include "../src/core/PresentationController.h"
#include "../src/licensing/FileLicenseStore.h"
#include "fixtures/LicenseFixture.h"
#include <QFile>
#include <QJsonDocument>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
class LicenseTest : public QObject {
    Q_OBJECT
  private slots:
    void invalidExpiredRevokedAndCopiedLeases() {
        using namespace LicenseCrypto;
        const auto seed = random(32), key = publicKey(seed);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto lease = fixtureLease(seed, "machine", "license", now, now + 604800);
        QVERIFY(LicenseManager::validateLease(lease, key, "machine", "license", now));
        QVERIFY(!LicenseManager::validateLease(lease, key, "copy", "license", now));
        QVERIFY(!LicenseManager::validateLease(lease, key, "machine", "other", now));
        QVERIFY(!LicenseManager::validateLease(lease, publicKey(random(32)), "machine", "license",
                                               now));
        QVERIFY(!LicenseManager::validateLease(lease, key, "machine", "license", now + 604800));
        QVERIFY(!LicenseManager::validateLease(
            fixtureLease(seed, "machine", "license", now + 301, now + 1000), key, "machine",
            "license", now));
        QVERIFY(!LicenseManager::validateLease(
            fixtureLease(seed, "machine", "license", now, now + 604801), key, "machine", "license",
            now));
        QVERIFY(!LicenseManager::validateLease(
            fixtureLease(seed, "machine", "license", now, now + 100, "revoked"), key, "machine",
            "license", now));
        lease[10] = lease[10] == 'A' ? 'B' : 'A';
        QVERIFY(!LicenseManager::validateLease(lease, key, "machine", "license", now));
        QVERIFY(!LicenseManager::validateLease("invalid", key, "machine", "license", now));
    }
    void cachedAuthorizationAndEditingOnlyMode() {
        auto valid = fixtureLicense();
        QVERIFY(valid->liveAllowed());
        auto empty = fixtureLicense(false);
        QVERIFY(!empty->liveAllowed());
        PresentationController controller;
        controller.setLiveAuthorizationCheck([&] { return empty->liveAllowed(); });
        controller.takeLive("Blocked", {"Text"});
        QCOMPARE(controller.state().index, -1);
        controller.setLiveAuthorizationCheck([&] { return valid->liveAllowed(); });
        controller.takeLive("Allowed", {"One", "Two"});
        QCOMPARE(controller.state().index, 0);
        controller.setLiveAuthorizationCheck([&] { return empty->liveAllowed(); });
        QVERIFY(!controller.next());
        QVERIFY(!controller.previous());
        QVERIFY(!controller.setSlide(1));
        controller.revokeLiveAuthorization();
        QCOMPARE(controller.state().index, -1);
        QVERIFY(controller.snapshotItems().isEmpty());
    }
    void protectedDenialsClockRollbackAndMachineCopiesAreRejected() {
        auto denied = fixtureLicense(true, [](QJsonObject &state) { state["denied"] = true; });
        QVERIFY(!denied->liveAllowed());
        auto rollback = fixtureLicense(true, [](QJsonObject &state) {
            state["highWater"] = double(QDateTime::currentSecsSinceEpoch() + 3600);
        });
        QVERIFY(!rollback->liveAllowed());
        auto copy =
            fixtureLicense(true, [](QJsonObject &state) { state["machine"] = "other-machine"; });
        QVERIFY(!copy->liveAllowed());
        QVERIFY(copy->status().contains("another machine"));
        auto corrupt =
            fixtureLicense(true, [](QJsonObject &state) { state["lease"] = "tampered"; });
        QVERIFY(!corrupt->liveAllowed());
    }
#ifdef Q_OS_UNIX
    void encryptedActivationSurvivesRestartWithoutAnAuthenticationStore() {
        using namespace LicenseCrypto;
        QTemporaryDir directory;
        const auto path = directory.filePath("licensing/activation-state.bin");
        const auto authority = random(32), seed = random(32), binding = random(32);
        const auto install = QString::fromLatin1(installationId(publicKey(seed)));
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto state =
            QJsonDocument(
                QJsonObject{
                    {"v", 1},
                    {"seed", QString::fromLatin1(encode(seed))},
                    {"lease", QString::fromLatin1(fixtureLease(
                                  authority, install, "stored-activation", now, now + 86400))},
                    {"licenseId", "stored-activation"},
                    {"machine", QString::fromLatin1(encode(QCryptographicHash::hash(
                                    QSysInfo::machineUniqueId(), QCryptographicHash::Sha256)))},
                    {"highWater", double(now)},
                    {"denied", false}})
                .toJson(QJsonDocument::Compact);
        QString error;
        QVERIFY2(fileLicenseStore(path, binding)->write(state, &error), qPrintable(error));
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto envelope = saved.readAll();
        QVERIFY(!envelope.contains(encode(seed)));
        QVERIFY(!envelope.contains("stored-activation"));
        struct stat fileInfo{}, directoryInfo{};
        QVERIFY(::stat(QFile::encodeName(path).constData(), &fileInfo) == 0);
        QVERIFY(::stat(QFile::encodeName(QFileInfo(path).absolutePath()).constData(),
                       &directoryInfo) == 0);
        QCOMPARE(fileInfo.st_mode & 0777, mode_t(0600));
        QCOMPARE(directoryInfo.st_mode & 0777, mode_t(0700));
        const LicenseConfiguration configuration{QUrl("https://127.0.0.1:1"), publicKey(authority)};
        for (int launch = 0; launch < 2; ++launch) {
            LicenseManager manager(nullptr, configuration, fileLicenseStore(path, binding));
            manager.start();
            QVERIFY(manager.liveAllowed());
            QCOMPARE(manager.installationId(), install);
        }
    }
    void activationEnvelopeRejectsTamperingAndCopiesToAnotherBinding() {
        using namespace LicenseCrypto;
        QTemporaryDir directory;
        const auto path = directory.filePath("licensing/activation-state.bin");
        const auto key = random(32);
        QString error;
        QByteArray state;
        QVERIFY(fileLicenseStore(path, key)->write("private activation fixture", &error));
        QVERIFY(!fileLicenseStore(path, random(32))->read(&state, &error));
        QVERIFY(state.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        auto envelope = file.readAll();
        envelope[18] ^= 1;
        file.seek(0);
        QCOMPARE(file.write(envelope), envelope.size());
        file.close();
        QVERIFY(!fileLicenseStore(path, key)->read(&state, &error));
        QVERIFY(state.isEmpty());
    }
    void activationPersistenceRejectsSymlinksAndKeepsPriorStateOnInvalidWrites() {
        using namespace LicenseCrypto;
        QTemporaryDir directory;
        QString error;
        QByteArray state;
        const auto key = random(32), expected = QByteArray("keep existing activation");
        const auto path = directory.filePath("licensing/activation-state.bin");
        auto store = fileLicenseStore(path, key);
        QVERIFY(store->write(expected, &error));
        QVERIFY(!store->write(QByteArray(65537, 'x'), &error));
        QVERIFY(store->read(&state, &error));
        QCOMPARE(state, expected);
        const auto link = directory.filePath("licensing/link.bin");
        QVERIFY(QFile::link(path, link));
        QVERIFY(!fileLicenseStore(link, key)->read(&state, &error));
        QVERIFY(!fileLicenseStore(link, key)->write("replace", &error));
        QVERIFY(store->read(&state, &error));
        QCOMPARE(state, expected);
    }
#endif
    void unconfiguredBuildAndStorageFailuresDenyOutput() {
        auto store = std::make_unique<MemoryLicenseStore>();
        store->writable = false;
        LicenseManager manager(nullptr, {}, std::move(store));
        manager.start();
        QVERIFY(!manager.liveAllowed());
        QVERIFY(manager.status().contains("unavailable"));
        LicenseManager unconfigured(nullptr, {}, std::make_unique<MemoryLicenseStore>());
        unconfigured.start();
        QVERIFY(!unconfigured.configured());
        QVERIFY(!unconfigured.liveAllowed());
    }
};
QTEST_GUILESS_MAIN(LicenseTest)
#include "license_test.moc"
