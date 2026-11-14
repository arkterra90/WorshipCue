#!/usr/bin/env python3
"""Build, deploy, validate and package a native WorshipCue release.

No upload/publishing is performed. Signing credentials stay in the OS credential
store. Run on macOS for DMG, or Windows x64 for an Inno Setup installer.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from release_support import (audit_macos, bundle_external_libraries, check_installed,
    machos, minimum_macos, output, require, scan_distribution, sha256)

SOURCE = Path(__file__).resolve().parents[1]


def version():
    return re.search(r'project\(WorshipCue VERSION ([0-9.]+)', (SOURCE / 'CMakeLists.txt').read_text())[1]


def run(command, log, **kwargs):
    with log.open('a', encoding='utf8') as stream:
        stream.write('\n' + subprocess.list2cmdline([str(part) for part in command]) + '\n')
        stream.flush()
        result = subprocess.run([str(part) for part in command], stdout=stream, stderr=subprocess.STDOUT, **kwargs)
    require(result.returncode == 0, f'Command failed ({result.returncode}); see {log}')


def copy_dependency_notices(legal, qt, openssl, copied):
    provenance = []
    roots = {qt.resolve(), openssl.resolve()}
    # Homebrew formula roots for third-party dylibs added to the deployment.
    for item in copied:
        source = Path(item['source'])
        formula = next((p for p in source.parents if (p / 'INSTALL_RECEIPT.json').is_file()), None)
        if formula:
            roots.add(formula.resolve())
    for root in sorted(roots):
        name = root.name if root.parent.name != 'Cellar' else root.parent.name + '-' + root.name
        destination = legal / 'dependencies' / (root.parent.name + '-' + name)
        candidates = [p for p in root.iterdir() if p.is_file() and
                      (p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')) or p.name == 'sbom.spdx.json')]
        candidates += list((root / 'share/qt/sbom').glob('*.spdx'))
        candidates += list((root / 'share/openssl').glob('copyright'))
        for source in candidates:
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination / source.name)
        provenance.append({'component': root.parent.name + '/' + root.name,
                           'notices': [p.name for p in candidates]})
    return provenance


def sign_macos(app, identity, log):
    options = ['--force', '--sign', identity]
    options += ['--options', 'runtime', '--timestamp'] if identity != '-' else ['--timestamp=none']
    for binary in sorted(machos(app), key=lambda p: len(p.parts), reverse=True):
        run(['/usr/bin/codesign', *options, binary], log)
    for framework in sorted(app.rglob('*.framework'), key=lambda p: len(p.parts), reverse=True):
        run(['/usr/bin/codesign', *options, framework], log)
    run(['/usr/bin/codesign', *options, app], log)
    run(['/usr/bin/codesign', '--verify', '--deep', '--strict', '--verbose=2', app], log)


def macos_package(args, stage, work, log, report):
    app = stage / 'WorshipCue.app'
    deploy = args.qt_prefix / 'bin/macdeployqt'
    require(deploy.is_file(), f'macdeployqt not found in {args.qt_prefix}')
    print('Deploying Qt, plugins and the MCP companion…', flush=True)
    deployment_log_start = log.stat().st_size
    run([deploy, app, '-no-codesign', '-verbose=2',
         '-executable=' + str(app / 'Contents/MacOS/WorshipCueMcp')], log)
    # Deploy the plugin families used by this Widgets application. Do not ship
    # unrelated SDK addons such as the QML virtual keyboard or designer plugins.
    allowed = {'platforms': {'libqcocoa.dylib'}, 'sqldrivers': {'libqsqlite.dylib'},
               'imageformats': None, 'iconengines': None, 'styles': None,
               'tls': None, 'multimedia': None, 'networkinformation': None}
    for family in (app / 'Contents/PlugIns').iterdir():
        if family.name not in allowed:
            if family.is_dir(): shutil.rmtree(family)
            else: family.unlink()
        elif allowed[family.name] is not None:
            for plugin in family.iterdir():
                if plugin.name not in allowed[family.name]:
                    if plugin.is_dir(): shutil.rmtree(plugin)
                    else: plugin.unlink()
    (app / 'Contents/Resources/qt.conf').write_text(
        '[Paths]\nPrefix=.\nBinaries=MacOS\nLibraries=Frameworks\nPlugins=PlugIns\n', encoding='utf8')
    copied = bundle_external_libraries(app)
    deployment_text = log.read_text(encoding='utf8')[deployment_log_start:]
    copied += [{'name': Path(source).name, 'source': source}
               for source in re.findall(r'copied: "([^"]+)"', deployment_text)]
    legal = app / 'Contents/Resources/Legal'
    report['dependencyNotices'] = copy_dependency_notices(legal, args.qt_prefix, args.openssl_root, copied)
    report['runtime'] = audit_macos(app, args.architecture)
    info_path = app / 'Contents/Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    info['LSMinimumSystemVersion'] = report['runtime']['minimumMacOS']
    if not args.signing_identity:
        info['CFBundleDisplayName'] = 'WorshipCue (Unsigned Development)'
    info_path.write_bytes(plistlib.dumps(info))
    sign_macos(app, args.signing_identity or '-', log)
    print('Checking relocated GUI, offline resources, migrations, TLS, video and MCP…', flush=True)
    report['applicationChecks'] = check_installed(app, SOURCE / 'tests/fixtures/media-colors.mov', args.architecture)
    if args.notary_profile:
        archive = work / 'WorshipCue-notarize.zip'
        run(['/usr/bin/ditto', '-c', '-k', '--keepParent', app, archive], log)
        run(['xcrun', 'notarytool', 'submit', archive, '--keychain-profile', args.notary_profile, '--wait', '--timeout', '20m'], log)
        run(['xcrun', 'stapler', 'staple', app], log)
        run(['xcrun', 'stapler', 'validate', app], log)
    image_root = work / 'disk image'
    image_root.mkdir()
    shutil.move(str(app), image_root / app.name)
    (image_root / 'Applications').symlink_to('/Applications', target_is_directory=True)
    (image_root / 'Install WorshipCue.txt').write_text(
        'Drag WorshipCue.app to Applications, then launch it from Applications.\n'
        'Editing is free; activate a license in Settings for live output.\n' +
        ('Unsigned development build: not signed with Developer ID or notarized.\n' if not args.signing_identity else ''), encoding='utf8')
    artifact = work / (report['artifactStem'] + '.dmg')
    print('Creating and testing the disk image…', flush=True)
    run(['/usr/bin/hdiutil', 'create', '-volname', 'WorshipCue ' + version(), '-srcfolder', image_root,
         '-format', 'UDZO', '-fs', 'HFS+', artifact], log)
    if args.signing_identity:
        run(['/usr/bin/codesign', '--force', '--sign', args.signing_identity, '--timestamp', artifact], log)
    if args.notary_profile:
        run(['xcrun', 'notarytool', 'submit', artifact, '--keychain-profile', args.notary_profile, '--wait', '--timeout', '20m'], log)
        run(['xcrun', 'stapler', 'staple', artifact], log)
        run(['xcrun', 'stapler', 'validate', artifact], log)
        run(['/usr/sbin/spctl', '--assess', '--type', 'open', '--context', 'context:primary-signature', '--verbose=2', artifact], log)
    # Read the final image, then install to a fresh folder with spaces. This
    # exercises the delivered artifact rather than only the staging directory.
    run(['/usr/bin/hdiutil', 'verify', artifact], log)
    mount = work / 'mounted image'
    mount.mkdir()
    run(['/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, artifact], log)
    try:
        installed = work / 'Relocated Applications'
        installed.mkdir()
        target = installed / 'WorshipCue.app'
        run(['/usr/bin/ditto', mount / 'WorshipCue.app', target], log)
        report['installedChecks'] = check_installed(target, SOURCE / 'tests/fixtures/media-colors.mov', args.architecture)
        run(['/usr/bin/codesign', '--verify', '--deep', '--strict', target], log)
    finally:
        run(['/usr/bin/hdiutil', 'detach', mount], log)
    report['installerTest'] = 'DMG verified, mounted read-only, copied, launched in isolation and detached'
    return artifact


def windows_package(args, stage, work, log, report):
    deploy = args.qt_prefix / 'bin/windeployqt.exe'
    require(deploy.is_file(), 'windeployqt.exe is missing')
    bin_dir = stage / 'bin'
    print('Deploying Windows Qt, compiler runtimes, OpenSSL and plugins…', flush=True)
    run([deploy, '--release', '--compiler-runtime', '--dir', bin_dir,
         bin_dir / 'WorshipCue.exe', bin_dir / 'WorshipCueMcp.exe'], log)
    dlls = list((args.openssl_root / 'bin').glob('libcrypto*.dll')) + list((args.openssl_root / 'bin').glob('libssl*.dll'))
    require(any('crypto' in p.name for p in dlls), 'OpenSSL runtime DLLs missing from --openssl-root/bin')
    for dll in dlls:
        shutil.copy2(dll, bin_dir / dll.name)
    # windeployqt can copy a VC redistributable installer instead of app-local
    # DLLs. A per-user installer must not depend on an elevated redist install.
    redist = args.msvc_redist_root or os.environ.get('VCToolsRedistDir')
    if redist:
        for runtime in Path(redist).glob('x64/Microsoft.VC*.CRT/*.dll'):
            shutil.copy2(runtime, bin_dir / runtime.name)
    require((bin_dir / 'msvcp140.dll').is_file() and (bin_dir / 'vcruntime140.dll').is_file(),
            'Missing app-local MSVC runtime. Use a VS developer shell or --msvc-redist-root')
    (bin_dir / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n', encoding='utf8')
    report['dependencyNotices'] = copy_dependency_notices(stage / 'share/WorshipCue/Legal', args.qt_prefix, args.openssl_root, [])
    sign = None
    if args.windows_cert_thumbprint:
        require(args.signtool, 'Specify --signtool or put signtool.exe in PATH')
        sign = [args.signtool, 'sign', '/sha1', args.windows_cert_thumbprint, '/fd', 'SHA256', '/tr', args.timestamp_url, '/td', 'SHA256']
        for binary in (bin_dir / 'WorshipCue.exe', bin_dir / 'WorshipCueMcp.exe'):
            run([*sign, binary], log)
            run([args.signtool, 'verify', '/pa', binary], log)
    report['applicationChecks'] = check_installed(stage, SOURCE / 'tests/fixtures/media-colors.mov')
    require(args.iscc and Path(args.iscc).is_file(), 'Inno Setup 6 compiler missing; supply --iscc')
    command = [args.iscc, '/DVersion=' + version(), '/DStage=' + str(stage), '/DOutputDir=' + str(work),
               '/DArtifactName=' + report['artifactStem'], '/DAppIcon=' + str(SOURCE / 'assets/icons/WorshipCue.ico')]
    if sign:
        command += ['/DSigned', '/Sworshipcue=' + subprocess.list2cmdline(sign) + ' $f']
    run([*command, SOURCE / 'packaging/windows/WorshipCue.iss'], log)
    artifact = work / (report['artifactStem'] + '.exe')
    if sign:
        run([args.signtool, 'verify', '/pa', artifact], log)
    print('Testing native installation and uninstallation…', flush=True)
    installed = work / 'Installed WorshipCue with spaces'
    # The test uses an isolated install root and normal per-user registry entry.
    # Never replace an existing user's installation during a packaging check.
    existing = subprocess.run(['reg', 'query', r'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{863A109D-5E0A-4DA4-ADE4-4100DA9C0C54}_is1'], capture_output=True)
    require(existing.returncode != 0, 'An existing WorshipCue installation is registered; run installer checks in a clean Windows user/VM')
    run([artifact, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/DIR=' + str(installed)], log)
    try:
        report['installedChecks'] = check_installed(installed, SOURCE / 'tests/fixtures/media-colors.mov')
        sentinel = installed / 'user-kept-file.txt'
        sentinel.write_text('Uninstall must preserve untracked user files', encoding='utf8')
    finally:
        uninstall = installed / 'unins000.exe'
        require(uninstall.is_file(), 'Installer did not create an uninstaller')
        run([uninstall, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'], log)
    require(not (installed / 'bin/WorshipCue.exe').exists(), 'Uninstall left the application executable')
    require(sentinel.is_file(), 'Uninstall removed untracked user data')
    report['installerTest'] = 'Native silent install, relocated GUI/MCP, uninstall, and user-file preservation'
    return artifact


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--openssl-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output-dir', type=Path, default=SOURCE / 'dist')
    parser.add_argument('--architecture', choices=['arm64', 'x86_64', 'x64'])
    parser.add_argument('--signing-identity', help='macOS Developer ID Application identity in Keychain')
    parser.add_argument('--notary-profile', help='Existing notarytool Keychain profile; never a password')
    parser.add_argument('--windows-cert-thumbprint', help='Existing Windows certificate-store SHA1 thumbprint')
    parser.add_argument('--signtool', default=shutil.which('signtool.exe'))
    parser.add_argument('--timestamp-url', default='http://timestamp.digicert.com')
    parser.add_argument('--iscc', default=shutil.which('ISCC.exe') or (str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Inno Setup 6/ISCC.exe') if sys.platform == 'win32' else None))
    parser.add_argument('--msvc-redist-root', type=Path, help='Visual Studio VC/Redist/MSVC/version directory')
    parser.add_argument('--generator')
    parser.add_argument('--skip-build', action='store_true', help='Package a previously configured matching Release build')
    parser.add_argument('--overwrite', action='store_true')
    args = parser.parse_args(argv)
    require(sys.platform in ('darwin', 'win32'), 'Run on macOS or Windows with its native toolchain')
    mac = sys.platform == 'darwin'
    require(not args.notary_profile or args.signing_identity, 'Notarization requires a Developer ID signing identity')
    require(not (mac and args.windows_cert_thumbprint) and not (not mac and args.signing_identity), 'Signing option does not match the native platform')
    if args.signing_identity:
        require(args.signing_identity.startswith('Developer ID Application:'), 'Use a Developer ID Application identity for distribution')
    if args.windows_cert_thumbprint:
        require(re.fullmatch('[A-Fa-f0-9]{40}', args.windows_cert_thumbprint), 'Invalid signing certificate thumbprint')
    args.architecture = args.architecture or (platform.machine() if mac else 'x64')
    require(args.architecture in (('arm64', 'x86_64') if mac else ('x64',)), 'Unsupported platform/architecture combination')
    args.qt_prefix = args.qt_prefix.resolve(strict=True)
    args.openssl_root = args.openssl_root.resolve(strict=True)
    args.channel = 'release' if args.signing_identity or args.windows_cert_thumbprint else 'unsigned'
    args.build_dir = (args.build_dir or SOURCE / 'build-release' / (('macos-' if mac else 'windows-') + args.architecture + '-' + args.channel)).resolve()
    args.output_dir = args.output_dir.resolve()
    require(args.build_dir != SOURCE and args.output_dir != SOURCE, 'Choose dedicated build/output directories')
    return args


def main():
    args = parse_args()
    mac = sys.platform == 'darwin'
    designation = 'notarized' if args.notary_profile else 'signed-unnotarized' if args.signing_identity else 'signed' if args.windows_cert_thumbprint else 'unsigned'
    stem = f'WorshipCue-{version()}-{("macOS" if mac else "Windows")}-{args.architecture}-{designation}'
    destination = args.output_dir / (stem + ('.dmg' if mac else '.exe'))
    require(args.overwrite or not destination.exists(), f'Artifact already exists: {destination}; use --overwrite to replace it')
    args.build_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    log = args.build_dir / 'package-release.log'
    log.write_text('', encoding='utf8')
    if not args.skip_build:
        print('Configuring and building Release…', flush=True)
        configure = ['cmake', '-S', SOURCE, '-B', args.build_dir, '-DCMAKE_BUILD_TYPE=Release', '-DWORSHIPCUE_BUILD_TESTS=OFF',
                     '-DWORSHIPCUE_RELEASE_CHANNEL=' + args.channel, '-DCMAKE_PREFIX_PATH=' + str(args.qt_prefix), '-DOPENSSL_ROOT_DIR=' + str(args.openssl_root)]
        if args.generator:
            configure += ['-G', args.generator]
        if mac:
            minimum = minimum_macos(args.qt_prefix / 'lib/QtCore.framework/Versions/A/QtCore')
            configure += ['-DCMAKE_OSX_ARCHITECTURES=' + args.architecture, '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + '.'.join(map(str, minimum))]
        run(configure, log)
        run(['cmake', '--build', args.build_dir, '--config', 'Release', '--target', 'WorshipCue', '--parallel', '4'], log)
    cache = (args.build_dir / 'CMakeCache.txt').read_text()
    require('WORSHIPCUE_RELEASE_CHANNEL:STRING=' + args.channel in cache, 'Cached build channel does not match requested signing mode')
    # A private work directory avoids mixing stale dependencies with a new build.
    with tempfile.TemporaryDirectory(prefix='release work ', dir=args.build_dir) as directory:
        work = Path(directory)
        stage = work / 'stage'
        run(['cmake', '--install', args.build_dir, '--config', 'Release', '--prefix', stage], log)
        scan_distribution(stage)
        report = {'version': version(), 'platform': sys.platform, 'architecture': args.architecture, 'channel': args.channel,
                  'signing': designation, 'artifactStem': stem, 'createdUTC': datetime.now(timezone.utc).isoformat(),
                  'cleanMachineTest': False}
        try:
            report['sourceRevision'] = output('git', '-C', SOURCE, 'rev-parse', 'HEAD').strip()
            report['sourceDirty'] = bool(output('git', '-C', SOURCE, 'status', '--porcelain', '--untracked-files=normal').strip())
        except subprocess.CalledProcessError:
            report['sourceRevision'] = 'unknown'
        artifact = macos_package(args, stage, work, log, report) if mac else windows_package(args, stage, work, log, report)
        require(artifact.is_file(), 'Packaging did not produce an artifact')
        report['sha256'] = sha256(artifact)
        report['bytes'] = artifact.stat().st_size
        shutil.copy2(artifact, destination)
        destination.with_suffix(destination.suffix + '.sha256').write_text(report['sha256'] + '  ' + destination.name + '\n', encoding='utf8')
        destination.with_suffix(destination.suffix + '.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f'Created and checked: {destination}', flush=True)
    print(f'Signing: {designation}. Clean-machine validation is still required before publication.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
