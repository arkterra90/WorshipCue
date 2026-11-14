#pragma once
#include "LicenseStore.h"
// Owner-only, atomic local activation persistence. Used by macOS to avoid
// application-signature-dependent Keychain prompts; Windows retains DPAPI.
std::unique_ptr<LicenseStore> fileLicenseStore(const QString &path, const QByteArray &bindingKey);
