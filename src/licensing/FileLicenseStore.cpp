#include "FileLicenseStore.h"
#include "LicenseCrypto.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <memory>
#include <openssl/evp.h>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
constexpr qint64 maximumStateBytes = 65536;
constexpr qint64 maximumFileBytes = maximumStateBytes + 32;
const QByteArray envelopeMagic("WCA2");
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
QByteArray seal(const QByteArray &plain, const QByteArray &key) {
    if (key.size() != 32 || plain.isEmpty() || plain.size() > maximumStateBytes)
        return {};
    const auto nonce = LicenseCrypto::random(12);
    if (nonce.size() != 12)
        return {};
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray cipher(plain.size() + 16, 0), tag(16, 0);
    int size = 0, finalSize = 0, ignored = 0;
    if (!context ||
        EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) != 1 ||
        EVP_EncryptUpdate(context.get(), nullptr, &ignored,
                          reinterpret_cast<const unsigned char *>(envelopeMagic.constData()),
                          envelopeMagic.size()) != 1 ||
        EVP_EncryptUpdate(context.get(), reinterpret_cast<unsigned char *>(cipher.data()), &size,
                          reinterpret_cast<const unsigned char *>(plain.constData()),
                          plain.size()) != 1 ||
        EVP_EncryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(cipher.data()) + size,
                            &finalSize) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) != 1)
        return {};
    cipher.resize(size + finalSize);
    return envelopeMagic + nonce + cipher + tag;
}
bool unseal(const QByteArray &sealed, const QByteArray &key, QByteArray *plain) {
    if (key.size() != 32 || sealed.size() <= 32 || sealed.size() > maximumFileBytes ||
        !sealed.startsWith(envelopeMagic))
        return false;
    const auto nonce = sealed.mid(4, 12), cipher = sealed.mid(16, sealed.size() - 32);
    auto tag = sealed.right(16);
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray candidate(cipher.size() + 16, 0);
    int size = 0, finalSize = 0, ignored = 0;
    if (!context ||
        EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) != 1 ||
        EVP_DecryptUpdate(context.get(), nullptr, &ignored,
                          reinterpret_cast<const unsigned char *>(envelopeMagic.constData()),
                          envelopeMagic.size()) != 1 ||
        EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(candidate.data()), &size,
                          reinterpret_cast<const unsigned char *>(cipher.constData()),
                          cipher.size()) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(), tag.data()) != 1 ||
        EVP_DecryptFinal_ex(context.get(),
                            reinterpret_cast<unsigned char *>(candidate.data()) + size,
                            &finalSize) != 1)
        return false;
    candidate.resize(size + finalSize);
    *plain = candidate;
    return true;
}
class FileStore final : public LicenseStore {
  public:
    explicit FileStore(QString path, QByteArray key)
        : path_(std::move(path)), key_(std::move(key)) {}
    bool read(QByteArray *state, QString *error) override {
        state->clear();
#ifdef Q_OS_UNIX
        if (key_.size() != 32 || !secureDirectory(error))
            return false;
        const auto path = QFile::encodeName(path_);
        const int descriptor = ::open(path.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (descriptor < 0) {
            if (errno == ENOENT)
                return true;
            return fail(error);
        }
        struct stat info{};
        if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_uid != ::geteuid() || info.st_size <= 0 || info.st_size > maximumFileBytes ||
            ::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) {
            ::close(descriptor);
            return fail(error);
        }
        QFile file;
        if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
            ::close(descriptor);
            return fail(error);
        }
        const auto sealed = file.read(maximumFileBytes + 1);
        if (file.error() != QFileDevice::NoError || sealed.size() > maximumFileBytes ||
            !unseal(sealed, key_, state)) {
            state->clear();
            return fail(error);
        }
        return true;
#else
        Q_UNUSED(state);
        return fail(error);
#endif
    }
    bool write(const QByteArray &state, QString *error) override {
#ifdef Q_OS_UNIX
        if (state.isEmpty() || state.size() > maximumStateBytes || !secureDirectory(error))
            return fail(error);
        struct stat info{};
        const auto path = QFile::encodeName(path_);
        if (::lstat(path.constData(), &info) == 0) {
            if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid())
                return fail(error);
        } else if (errno != ENOENT)
            return fail(error);
        const auto sealed = seal(state, key_);
        if (sealed.isEmpty())
            return fail(error);
        QSaveFile file(path_);
        // Restrict the empty temporary file before writing any installation key.
        // Direct-write fallback remains disabled so failed commits keep old state.
        if (!file.open(QIODevice::WriteOnly) ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
            ::fchmod(file.handle(), S_IRUSR | S_IWUSR) != 0 ||
            file.write(sealed) != sealed.size() || !file.commit())
            return fail(error);
        return true;
#else
        Q_UNUSED(state);
        return fail(error);
#endif
    }

  private:
    QString path_;
    QByteArray key_;
    bool fail(QString *error) const {
        if (error)
            *error =
                QStringLiteral("Saved activation could not be accessed safely. Editing remains "
                               "available. Check this user's application-data permissions.");
        return false;
    }
    bool secureDirectory(QString *error) const {
#ifdef Q_OS_UNIX
        const auto directory = QFileInfo(path_).absolutePath();
        const auto bytes = QFile::encodeName(directory);
        if (!QDir().mkpath(directory))
            return fail(error);
        struct stat info{};
        if (::lstat(bytes.constData(), &info) != 0 || !S_ISDIR(info.st_mode) ||
            info.st_uid != ::geteuid() || ::chmod(bytes.constData(), S_IRWXU) != 0)
            return fail(error);
        return true;
#else
        return fail(error);
#endif
    }
};
} // namespace
std::unique_ptr<LicenseStore> fileLicenseStore(const QString &path, const QByteArray &bindingKey) {
    return std::make_unique<FileStore>(path, bindingKey);
}
