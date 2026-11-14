# Building and distributing WorshipCue

Priority 10 adds a native packaging pipeline. It builds from the current source tree, installs into an empty staging directory, deploys dependencies, validates that installed runtime, creates a native artifact, and tests that artifact. It never uploads a release or bundles licensing/account credentials.

## Current validation status

An **unsigned Apple Silicon development DMG** has been built and tested on macOS 15.7.4. The local Qt 6.12.0 runtime and dependencies require **macOS 15.0 or newer** according to their Mach-O minimum-version commands. The package contains the GUI, MCP companion, 16 offline Help articles, 20 backgrounds, application icons, compiled SQLite migrations, licensing client/public authority, runtime plugins/libraries, and dependency notices.

The DMG was verified, mounted read-only, copied to a different directory with spaces, and checked there. Both staged and copied applications passed isolated GUI startup, actual H.264 decoding, TLS backend availability, editor/database creation, resource loading, and MCP creation/edit/persistence tests. All 24 desktop CTest suites pass, including seven packaging safeguard checks. These checks remove developer plugin/library paths and use temporary data. They do not prove behavior on an OS image without developer software.

**Not yet verified:** Windows compilation/install/uninstall, Intel Mac artifacts, Developer ID signing, notarization, Windows Authenticode, Gatekeeper acceptance of a downloaded release, or installation on clean macOS/Windows machines. A Windows installer recipe and manual CI workflow are included; no Windows installer is claimed as built. Public distribution readiness remains pending those release gates.

Generated artifacts are ignored under `dist/`:

- `WorshipCue-0.3.0-macOS-arm64-unsigned.dmg`
- Its `.sha256` checksum and `.json` build/verification report

The report records build channel, signing state, source revision/dirty state, dependency notices/versions, architecture, required macOS version, checks performed, and `cleanMachineTest: false`. Signing and timestamps prevent bit-for-bit identical artifacts; repeatability here means the same documented build/deploy/validation steps. Pin the toolchain/dependencies and commit the exact release source before publishing.

## Build an unsigned macOS DMG

Install Xcode command-line tools, Python 3.10+, CMake, Qt 6.4+, and OpenSSL development files. The current local setup uses Homebrew Qt 6.12.0 and OpenSSL 3.6.4:

```sh
python3 tools/package-release.py \
  --qt-prefix /opt/homebrew/opt/qt \
  --openssl-root /opt/homebrew/opt/openssl@3
```

The script creates an isolated Release build under `build-release/`, explicitly marks its channel **unsigned**, and calls `macdeployqt` for both executables. It retains the Widgets application plugin families for Cocoa, images/icons, styles, TLS/network information, SQLite, and multimedia. Unrelated SDK extensions such as QML virtual keyboard and designer plugins are excluded. Remaining third-party libraries are copied and rewritten to bundle-relative load names. All Mach-O images are checked for the target architecture, dependency closure, escaping symlinks, and external library search paths. The highest minimum macOS version across the actual runtime is written to Info.plist.

The complete app is signed inside-out with an ad-hoc signature for local integrity checks. **This is not a Developer ID signature or notarization.** The filename, Finder display name, window title, and report distinguish this development build. The image contains an Applications shortcut and installation instructions. Drag the app to Applications, then launch it there. Editing remains available; live outputs require license activation.

The packager verifies the final DMG, mounts it read-only, copies the app into a fresh temporary Applications directory, runs the relocated GUI/MCP checks, verifies the copied signature, and detaches the image. Generated work directories are cleaned after the operation. Build/deployment commands are logged in the selected build directory's `package-release.log`.

Useful options:

- `--architecture arm64` or `--architecture x86_64`: the Qt/OpenSSL/runtime libraries must all contain that architecture. A native Apple Silicon build is not automatically universal.
- `--build-dir PATH`, `--output-dir PATH`: choose dedicated generated directories.
- `--skip-build`: package an already-configured matching Release/channel build; do not use after source changes.
- `--overwrite`: replace this tool's existing artifact after a successful rebuild/check.

## Developer ID and notarization

Provision a **Developer ID Application** certificate/private key in the build user's Keychain and a `notarytool` Keychain credential profile. Do not place passwords, private keys, certificate exports, or Apple credentials in source, CMake flags, or shell history. Inspect installed identities with `security find-identity -v -p codesigning`.

```sh
python3 tools/package-release.py \
  --qt-prefix /PATH/TO/QT \
  --openssl-root /PATH/TO/OPENSSL \
  --signing-identity 'Developer ID Application: YOUR NAME (TEAMID)' \
  --notary-profile WorshipCue-notary
```

The script signs nested binaries/frameworks and the app with hardened runtime and secure timestamps, verifies the signature, notarizes a ZIP of the app, staples/verifies its ticket, creates/signs the DMG, notarizes/staples the image, and runs Gatekeeper assessment. A signing identity without a notary profile produces an explicitly **signed-unnotarized** artifact. Missing/rejected credentials, notarization failure, stapling failure, or failed Gatekeeper assessment stops the build; it cannot silently fall back to an unsigned release.

Apple's [notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow) describes account/profile setup. This workspace has no valid signing identity, so that path is prepared but untested. The licensing client now remembers activation in an owner-only encrypted macOS cache, avoiding Keychain login prompts on startup or unsigned updates. Older Keychain data is migrated only when it can be read noninteractively; otherwise users enter their activation code once. The earlier Keychain prompt observed during the first packaging check is superseded by this change.

## Build a Windows installer

Run on **Windows x64** in a Visual Studio 2022 developer shell with:

- Qt with MSVC 2022 x64 libraries, including Multimedia/Network/Concurrent and SQLite. The CI recipe pins Qt 6.8.3.
- OpenSSL headers/import libraries and its matching `bin/libcrypto*.dll` (and `libssl*.dll` where used). A vcpkg `openssl:x64-windows` installation is supported.
- Python 3.10+, CMake/Ninja or the Visual Studio generator, and Inno Setup 6.3+ (`ISCC.exe`).
- App-local MSVC runtime DLLs from `VCToolsRedistDir`, or `--msvc-redist-root` pointing to `VC/Redist/MSVC/VERSION`. A per-user installer must work without a privileged VC redistributable installation.

```powershell
python tools/package-release.py `
  --qt-prefix C:\Qt\6.8.3\msvc2022_64 `
  --openssl-root C:\vcpkg\installed\x64-windows `
  --generator Ninja `
  --iscc 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

The recipe builds Release, calls `windeployqt` for GUI and companion, copies matching OpenSSL/MSVC DLLs, writes relative Qt configuration, checks the installed GUI/MCP, and compiles `packaging/windows/WorshipCue.iss`. The installer includes a Windows multi-size icon and version information, stable application ID, per-user install under LocalAppData/Programs, a Start Menu shortcut, optional desktop shortcut, and an uninstaller. No administrator rights are required. Upgrades use Inno Setup's tracked installed files. The uninstaller has no recursive AppData deletion rule; library/media/preferences/DPAPI state are preserved.

On a clean Windows user/VM, the packager silently installs into a temporary path with spaces, runs the GUI/MCP checks, uninstalls, and verifies runtime removal and preservation of an untracked user file. It refuses this test if a WorshipCue installation is already registered. The recipe is unverified on Windows until that native run succeeds.

For Authenticode, install a code-signing certificate/private key in the Windows certificate store and make Windows SDK `signtool.exe` available:

```powershell
python tools/package-release.py ... `
  --windows-cert-thumbprint YOUR_40_HEX_CERTIFICATE_THUMBPRINT `
  --signtool 'C:\PATH\TO\signtool.exe'
```

The script signs/verifies GUI and companion and supplies Inno Setup's signing hook for the installer and uninstaller, using SHA-256 and an RFC 3161 timestamp. A certificate thumbprint is public; no certificate password/PFX is passed. See [Microsoft SignTool](https://learn.microsoft.com/dotnet/framework/tools/signtool-exe) and [Inno Setup signing](https://jrsoftware.org/inno-setup/documentation/topic_setup_signtool.htm).

## Test an existing installation

```sh
python3 tools/check-release-package.py '/PATH/TO/WorshipCue.app' \
  --architecture arm64 --media-fixture tests/fixtures/media-colors.mov
# Windows: supply the installation root containing bin/, rather than the EXE.
```

This checks runtime dependencies, bundled plugin search paths, resource/migration/licensing probes, and the existing MCP behavior checker. The GUI's `--verify-package` path creates only temporary content, never opens live outputs, never reads/writes OS licensing credentials, and never touches the operator's library/preferences. Media decoding is checked when a fixture is supplied. Schema migrations remain compiled into the repository code; no external SQL directory is needed by the desktop application.

```sh
python3 tests/release_packaging_test.py
cmake -S . -B build-tests -DWORSHIPCUE_BUILD_TESTS=ON
cmake --build build-tests --parallel 4
ctest --test-dir build-tests --output-on-failure
```

## CI and publication

`.github/workflows/release-build.yml` is a manually triggered workflow for macOS arm64 and Windows x64. It installs Qt 6.8.3, runs regressions, packages/tests unsigned artifacts, and uploads only `dist/*`. It does not publish a GitHub Release, deploy a website, or receive Cloudflare/license/private signing secrets. The workflow has not been run from this local checkout; commit/push the reviewed source and trigger it when ready. Its Windows build is the next validation step.

Before public release:

1. Commit the exact source; set the intended version in CMake (shared by GUI, MCP, metadata, and installer). Record toolchain/runtime versions and retain the generated report/checksum.
2. Run native tests/builds for each advertised OS/architecture. Verify on fresh machines/VMs with no Qt, OpenSSL, compiler, developer PATH, or prior library. Check first-run editing, activation, offline restart, content import, Help, MCP client connection, audio/video, displays, and preservation of an existing library through upgrade/uninstall.
3. Verify signed/notarized downloaded macOS artifacts through Gatekeeper and signed Windows setup/uninstaller through Authenticode/SmartScreen. Unsigned development artifacts remain labelled and are not represented as release-ready.
4. Review the actual dependency SBOMs/notices and source/relinking obligations for the chosen Qt/codec builds. The package includes original installed notices, Qt's standard license texts, and available SBOMs. Retain corresponding dependency source/build options, particularly for any FFmpeg/GPL/nonfree codec variants. Do not assume the same dependencies across Homebrew and Qt SDK builds.
5. Only after these gates pass, upload the tested installer/checksum to the download website and clearly state the supported OS/architecture. Website hosting/publication is a separate action; this pipeline does not perform it.

Qt's official [macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html) and [Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html) guides describe platform runtime tooling. The local packaging report is the evidence for what this particular artifact contains and what was tested.
