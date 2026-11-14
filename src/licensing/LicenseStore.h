#pragma once
#include <QByteArray>
#include <QString>
#include <memory>
// The complete state (installation seed, lease, clock high-water mark) is
// persisted together. macOS uses an owner-only file without authentication UI;
// Windows uses noninteractive DPAPI. Neither path depends on a local Boolean.
class LicenseStore {
  public:
    virtual ~LicenseStore() = default;
    virtual bool read(QByteArray *state, QString *error) = 0;
    virtual bool write(const QByteArray &state, QString *error) = 0;
};
std::unique_ptr<LicenseStore> nativeLicenseStore();
