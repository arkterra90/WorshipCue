#pragma once
#include <QByteArray>
namespace LicenseCrypto {
QByteArray random(int size);
QByteArray publicKey(const QByteArray &seed);
QByteArray sign(const QByteArray &seed, const QByteArray &message);
bool verify(const QByteArray &key, const QByteArray &message, const QByteArray &signature);
QByteArray encode(const QByteArray &data);
QByteArray decode(const QByteArray &data);
QByteArray installationId(const QByteArray &key);
} // namespace LicenseCrypto
