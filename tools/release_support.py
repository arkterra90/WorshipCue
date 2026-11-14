"""Shared release validation. Only standard-library Python is required."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile

MACHO = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def output(*args):
    return subprocess.check_output([str(a) for a in args], text=True, stderr=subprocess.STDOUT)


def is_macho(path):
    if not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in MACHO


def machos(root):
    return sorted({p.resolve() for p in root.rglob('*') if not p.is_symlink() and is_macho(p)})


def dependencies(binary):
    lines = output('/usr/bin/otool', '-L', binary).splitlines()
    # First entry for a shared image is its own install ID.
    start = 2 if binary.suffix == '.dylib' or '.framework/' in str(binary) else 1
    return [line.strip().split(' (', 1)[0] for line in lines[start:] if line[:1].isspace()]


def rpaths(binary):
    lines = output('/usr/bin/otool', '-l', binary).splitlines()
    return [lines[i+2].strip().split('path ', 1)[1].split(' (offset', 1)[0]
            for i, line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']


def system_dependency(name):
    return name.startswith(('/usr/lib/', '/System/Library/'))


def resolve_dependency(name, binary, app):
    contents = app.resolve() / 'Contents'
    if name.startswith('@rpath/'):
        target = contents / 'Frameworks' / name[len('@rpath/'):]
    elif name.startswith('@loader_path/'):
        target = binary.parent / name[len('@loader_path/'):]
    elif name.startswith('@executable_path/'):
        target = contents / 'MacOS' / name[len('@executable_path/'):]
    else:
        raise RuntimeError(f'External dependency in {binary.name}: {name}')
    require(target.resolve().is_relative_to(contents), f'Dependency escapes app bundle: {name}')
    require(target.is_file(), f'Missing bundled dependency in {binary.name}: {name}')
    return target.resolve()


def minimum_macos(binary):
    values, command = [], None
    for line in output('/usr/bin/otool', '-l', binary).splitlines():
        pieces = line.strip().split()
        if pieces[:1] == ['cmd'] and len(pieces) == 2:
            command = pieces[1]
        if len(pieces) == 2 and ((command == 'LC_BUILD_VERSION' and pieces[0] == 'minos')
            or (command == 'LC_VERSION_MIN_MACOSX' and pieces[0] == 'version')):
            values.append(tuple(int(part) for part in pieces[1].split('.')))
    return max(values, default=(0, 0))


def bundle_external_libraries(app):
    """macdeployqt handles Qt; close the remaining third-party dylib graph."""
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    pending = machos(app)
    checked, copied = set(), []
    while pending:
        binary = pending.pop()
        if binary in checked:
            continue
        checked.add(binary)
        binary.chmod(binary.stat().st_mode | stat.S_IWUSR)
        for dependency in dependencies(binary):
            if system_dependency(dependency) or dependency.startswith('@'):
                continue
            source = Path(dependency)
            require(source.is_absolute() and source.is_file(), f'Unresolved runtime library: {dependency}')
            require('.framework/' not in dependency, f'Qt deployment left an external framework: {dependency}')
            target = frameworks / source.name
            if target.exists():
                # A library copied by macdeployqt has already had its load names rewritten.
                # Reject collisions between distinct original third-party inputs.
                prior = next((item for item in copied if item['name'] == target.name), None)
                require(prior is None or Path(prior['source']).resolve() == source.resolve(), f'Runtime filename collision: {source.name}')
            else:
                shutil.copy2(source.resolve(), target)
                target.chmod(target.stat().st_mode | stat.S_IWUSR)
                subprocess.run(['/usr/bin/install_name_tool', '-id', '@rpath/' + target.name, str(target)], check=True)
                copied.append({'name': target.name, 'source': str(source.resolve())})
                pending.append(target.resolve())
            subprocess.run(['/usr/bin/install_name_tool', '-change', dependency, '@rpath/' + target.name, str(binary)], check=True)
    for binary in machos(app):
        for path in rpaths(binary):
            if path.startswith('/') and not system_dependency(path):
                subprocess.run(['/usr/bin/install_name_tool', '-delete_rpath', path, str(binary)], check=True)
    for name in ('WorshipCue', 'WorshipCueMcp'):
        binary = app / 'Contents/MacOS' / name
        if '@executable_path/../Frameworks' not in rpaths(binary):
            subprocess.run(['/usr/bin/install_name_tool', '-add_rpath', '@executable_path/../Frameworks', str(binary)], check=True)
    return copied


def audit_macos(app, architecture=None):
    contents = app / 'Contents'
    require((contents / 'MacOS/WorshipCue').is_file(), 'Missing GUI executable')
    require((contents / 'MacOS/WorshipCueMcp').is_file(), 'Missing MCP companion')
    require((contents / 'PlugIns/platforms/libqcocoa.dylib').is_file(), 'Missing Cocoa platform plugin')
    require((contents / 'PlugIns/sqldrivers/libqsqlite.dylib').is_file(), 'Missing SQLite plugin')
    require(list((contents / 'PlugIns/tls').glob('*.dylib')), 'Missing TLS plugins')
    require(list((contents / 'PlugIns/multimedia').glob('*.dylib')), 'Missing multimedia backend')
    binaries = machos(app)
    require(binaries, 'No Mach-O runtime found')
    minimum = (0, 0)
    for path in app.rglob('*'):
        if path.is_symlink():
            require(path.resolve().is_relative_to(app.resolve()), f'Bundle symlink escapes package: {path.name}')
    for binary in binaries:
        if architecture:
            subprocess.run(['/usr/bin/lipo', str(binary), '-verify_arch', architecture], check=True, capture_output=True)
        for dependency in dependencies(binary):
            if not system_dependency(dependency):
                resolve_dependency(dependency, binary, app)
        for path in rpaths(binary):
            require(not path.startswith('/') or system_dependency(path), f'External runtime search path in {binary.name}')
        minimum = max(minimum, minimum_macos(binary))
    return {'machOBinaries': len(binaries), 'minimumMacOS': '.'.join(map(str, minimum))}


def sanitized_environment(home):
    environment = {key: value for key, value in os.environ.items()
                   if key in {'SystemRoot', 'WINDIR', 'COMSPEC', 'DISPLAY', 'WAYLAND_DISPLAY'}}
    environment.update(HOME=str(home), USERPROFILE=str(home), TMPDIR=str(home), TMP=str(home), TEMP=str(home),
                       APPDATA=str(home / 'Roaming'), LOCALAPPDATA=str(home / 'Local'),
                       PATH=os.pathsep.join([str(Path(sys.executable).parent), os.defpath]))
    return environment


def scan_distribution(root):
    forbidden = {'worker-secrets.json', 'developer-license.json', '.env', '.dev.vars', 'license.dpapi', 'activation-state.bin', 'CMakeCache.txt'}
    for path in root.rglob('*'):
        require(path.name not in forbidden and path.suffix.lower() not in {'.p12', '.pfx', '.key'},
                f'Private/development file in package: {path.name}')
    require(not (root / 'licensing/secrets').exists(), 'Private licensing directory in package')


def check_installed(root, media_fixture=None, architecture=None):
    scan_distribution(root)
    mac = root.suffix == '.app'
    report = audit_macos(root, architecture) if mac else {}
    binary_dir = root / 'Contents/MacOS' if mac else root / 'bin'
    gui = binary_dir / ('WorshipCue' if mac else 'WorshipCue.exe')
    mcp = binary_dir / ('WorshipCueMcp' if mac else 'WorshipCueMcp.exe')
    require(gui.is_file() and mcp.is_file(), 'Missing installed GUI or MCP executable')
    legal = root / 'Contents/Resources/Legal' if mac else root / 'share/WorshipCue/Legal'
    require((legal / 'LICENSE').is_file() and (legal / 'THIRD_PARTY_NOTICES.md').is_file(), 'Missing legal notices')
    require((legal / 'licenses/LGPL-3.0-only.txt').is_file(), 'Missing Qt license texts')
    with tempfile.TemporaryDirectory(prefix='WorshipCue isolated runtime ') as directory:
        home = Path(directory)
        command = [str(gui), '--verify-package']
        if media_fixture:
            command += ['--media-fixture', str(media_fixture.resolve())]
        completed = subprocess.run(command, cwd=home, env=sanitized_environment(home), capture_output=True, text=True, timeout=30)
        require(completed.returncode == 0, f'Packaged GUI check failed: {completed.stdout[-1500:]} {completed.stderr[-1500:]}')
        result = json.loads(completed.stdout.strip())
        require(result.get('ok') and result.get('licenseConfigured') and result.get('libraryMigrations'), 'Invalid GUI package-check result')
        require(result.get('pluginPaths'), 'No bundled plugin search paths reported')
        require(all(Path(path).resolve().is_relative_to(root.resolve()) for path in result['pluginPaths']),
                'Qt is searching outside the installed runtime for plugins')
        result['pluginPaths'] = [str(Path(path).resolve().relative_to(root.resolve())) for path in result['pluginPaths']]
        libraries = Path(result.get('qtLibrariesPath', ''))
        require(libraries.resolve().is_relative_to(root.resolve()), 'Qt still reports an external library prefix')
        result['qtLibrariesPath'] = str(libraries.resolve().relative_to(root.resolve()))
        report.update(result)
    # Reuse the existing installed-MCP behavioral check and its independent temp library.
    import importlib.util
    spec = importlib.util.spec_from_file_location('mcp_package_check', Path(__file__).with_name('check-mcp-package.py'))
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.check_runtime(mcp)
    with tempfile.TemporaryDirectory(prefix='WorshipCue isolated MCP ') as directory:
        checker.smoke(mcp, Path(directory))
    report['mcp'] = True
    return report


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()
