#pragma once
#include <QString>
// A side-effect-free package probe: no normal user database/settings, Keychain,
// activation, MIDI devices, saved output routes, or live windows are touched.
int runPackageCheck(const QString &mediaFixture = {});
