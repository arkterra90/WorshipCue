#include "LicenseStore.h"
#include "FileLicenseStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#ifdef Q_OS_MACOS
#include <Security/Security.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>

#include <wincrypt.h>
#endif
namespace {
#ifdef Q_OS_MACOS
QByteArray localBindingKey() {
    const auto machine = QSysInfo::machineUniqueId();
    if (machine.isEmpty())
        return {};
    return QCryptographicHash::hash(QByteArray("org.worshipcue.desktop.activation-v2") + '\0' +
                                        machine + '\0' + QByteArray::number(::geteuid()),
                                    QCryptographicHash::Sha256);
}
#endif
class NativeStore final : public LicenseStore {
  public:
    bool read(QByteArray *state, QString *error) override {
        state->clear();
#ifdef Q_OS_MACOS
        auto local = fileLicenseStore(path(), localBindingKey());
        if (!local->read(state, error))
            return false;
        if (!state->isEmpty())
            return true;
        // Best-effort one-time migration. Never display a Keychain/password
        // prompt, including when a prior unsigned executable owned the item.
        const void *keys[] = {kSecClass,      kSecAttrService, kSecAttrAccount,
                              kSecReturnData, kSecMatchLimit,  kSecUseAuthenticationUI};
        const void *values[] = {kSecClassGenericPassword, CFSTR("org.worshipcue.desktop.license"),
                                CFSTR("installation-v1"), kCFBooleanTrue,
                                kSecMatchLimitOne,        kSecUseAuthenticationUIFail};
        auto query = CFDictionaryCreate(nullptr, keys, values, 6, &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        // kSecUseAuthenticationUIFail alone does not suppress legacy macOS
        // Keychain ACL dialogs. Scope the legacy UI policy to this read and
        // skip migration if it cannot be made noninteractive.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        Boolean previousInteraction = false;
        if (SecKeychainGetUserInteractionAllowed(&previousInteraction) != errSecSuccess ||
            SecKeychainSetUserInteractionAllowed(false) != errSecSuccess) {
            CFRelease(query);
            return true;
        }
        const auto status = SecItemCopyMatching(query, &result);
        SecKeychainSetUserInteractionAllowed(previousInteraction);
#pragma clang diagnostic pop
        CFRelease(query);
        if (status != errSecSuccess) {
            // Missing/inaccessible legacy state must never block opening the
            // editor. The user can activate once with their license code.
            return true;
        }
        const auto data = static_cast<CFDataRef>(result);
        const auto size = CFDataGetLength(data);
        if (size > 0 && size <= 65536)
            *state = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), size);
        CFRelease(result);
        if (state->isEmpty())
            return true;
        return local->write(*state, error);
#elif defined(Q_OS_WIN)
        QFile file(path());
        if (!file.exists())
            return true;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
            *error = "Windows licensing state could not be read.";
            return false;
        }
        auto data = file.readAll();
        DATA_BLOB input{DWORD(data.size()), reinterpret_cast<BYTE *>(data.data())}, output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                                CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            *error = "Windows could not unlock licensing state for this user and machine.";
            return false;
        }
        *state = QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        return true;
#else
        *error = "Protected license storage is supported on macOS and Windows.";
        return false;
#endif
    }
    bool write(const QByteArray &state, QString *error) override {
#ifdef Q_OS_MACOS
        return fileLicenseStore(path(), localBindingKey())->write(state, error);
#elif defined(Q_OS_WIN)
        DATA_BLOB input{DWORD(state.size()),
                        reinterpret_cast<BYTE *>(const_cast<char *>(state.constData()))},
            output{};
        if (!CryptProtectData(&input, L"WorshipCue installation", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            *error = "Windows could not protect licensing state.";
            return false;
        }
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QSaveFile file(path());
        bool ok =
            file.open(QIODevice::WriteOnly) &&
            file.write(reinterpret_cast<char *>(output.pbData), output.cbData) == output.cbData &&
            file.commit();
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        if (!ok)
            *error = "Windows licensing state could not be saved.";
        return ok;
#else
        Q_UNUSED(state);
        *error = "Protected license storage is supported on macOS and Windows.";
        return false;
#endif
    }

  private:
    static QString path() {
#ifdef Q_OS_MACOS
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
               "/licensing/activation-state.bin";
#else
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
               "/license.dpapi";
#endif
    }
};
} // namespace
std::unique_ptr<LicenseStore> nativeLicenseStore() { return std::make_unique<NativeStore>(); }
