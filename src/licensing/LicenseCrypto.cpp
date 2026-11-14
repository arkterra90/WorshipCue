#include "LicenseCrypto.h"
#include <QCryptographicHash>
#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>
namespace LicenseCrypto {
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
QByteArray random(int size) {
    QByteArray result(size, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(result.data()), size) != 1)
        return {};
    return result;
}
QByteArray publicKey(const QByteArray &seed) {
    if (seed.size() != 32)
        return {};
    Key key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                         reinterpret_cast<const unsigned char *>(seed.constData()),
                                         seed.size()),
            EVP_PKEY_free);
    QByteArray result(32, 0);
    size_t size = 32;
    if (!key ||
        EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char *>(result.data()),
                                    &size) != 1 ||
        size != 32)
        return {};
    return result;
}
QByteArray sign(const QByteArray &seed, const QByteArray &message) {
    if (seed.size() != 32)
        return {};
    Key key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                         reinterpret_cast<const unsigned char *>(seed.constData()),
                                         seed.size()),
            EVP_PKEY_free);
    Context ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    QByteArray result(64, 0);
    size_t size = 64;
    if (!key || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
        EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char *>(result.data()), &size,
                       reinterpret_cast<const unsigned char *>(message.constData()),
                       message.size()) != 1 ||
        size != 64)
        return {};
    return result;
}
bool verify(const QByteArray &pub, const QByteArray &message, const QByteArray &signature) {
    if (pub.size() != 32 || signature.size() != 64)
        return false;
    Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                        reinterpret_cast<const unsigned char *>(pub.constData()),
                                        pub.size()),
            EVP_PKEY_free);
    Context ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && ctx &&
           EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
           EVP_DigestVerify(
               ctx.get(), reinterpret_cast<const unsigned char *>(signature.constData()),
               signature.size(), reinterpret_cast<const unsigned char *>(message.constData()),
               message.size()) == 1;
}
QByteArray encode(const QByteArray &data) {
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray decode(const QByteArray &data) {
    const auto decoded = QByteArray::fromBase64(data, QByteArray::Base64UrlEncoding |
                                                          QByteArray::AbortOnBase64DecodingErrors);
    return encode(decoded) == data ? decoded : QByteArray{};
}
QByteArray installationId(const QByteArray &key) {
    return encode(QCryptographicHash::hash(key, QCryptographicHash::Sha256));
}
} // namespace LicenseCrypto
