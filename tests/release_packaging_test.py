"""Packaging safeguards and format checks, independent of Qt/native installers."""
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import release_support as release
spec = importlib.util.spec_from_file_location('package_release', ROOT / 'tools/package-release.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class ReleasePackagingTest(unittest.TestCase):
    def test_macos_minimum_ignores_library_source_and_sdk_versions(self):
        commands = '''Load command 1
 cmd LC_SOURCE_VERSION
 version 1230.1
Load command 2
 cmd LC_BUILD_VERSION
 platform 1
 minos 14.4
 sdk 15.4
Load command 3
 cmd LC_VERSION_MIN_MACOSX
 version 13.0
 sdk 15.0
'''
        with patch.object(release, 'output', return_value=commands):
            self.assertEqual(release.minimum_macos(Path('fixture')), (14, 4))

    def test_dependency_closure_rejects_external_missing_and_escaping_files(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'WorshipCue.app'
            binary = app / 'Contents/MacOS/WorshipCue'
            binary.parent.mkdir(parents=True)
            binary.touch()
            frameworks = app / 'Contents/Frameworks'
            frameworks.mkdir()
            target = frameworks / 'libcrypto.3.dylib'
            target.write_bytes(b'fixture')
            self.assertEqual(release.resolve_dependency('@rpath/libcrypto.3.dylib', binary, app), target.resolve())
            for value in ('/opt/homebrew/lib/crypto.dylib', '@rpath/missing.dylib', '@loader_path/../../../outside'):
                with self.assertRaises(RuntimeError):
                    release.resolve_dependency(value, binary, app)
            outside = Path(directory) / 'outside.dylib'
            outside.touch()
            (frameworks / 'escape.dylib').symlink_to(outside)
            with self.assertRaises(RuntimeError):
                release.resolve_dependency('@rpath/escape.dylib', binary, app)

    def test_runtime_environment_drops_developer_paths_credentials_and_plugins(self):
        with patch.dict(os.environ, {'QT_PLUGIN_PATH':'/private/developer/plugins', 'DYLD_LIBRARY_PATH':'/opt/homebrew/lib',
                'PLANNING_CENTER_ACCESS_TOKEN':'fixture-secret', 'CLOUDFLARE_API_TOKEN':'fixture-token'}, clear=False):
            environment = release.sanitized_environment(Path('/isolated'))
        self.assertNotIn('QT_PLUGIN_PATH', environment)
        self.assertNotIn('DYLD_LIBRARY_PATH', environment)
        self.assertNotIn('CLOUDFLARE_API_TOKEN', environment)
        self.assertNotIn('PLANNING_CENTER_ACCESS_TOKEN', environment)
        self.assertEqual(environment['LOCALAPPDATA'], '/isolated/Local')

    def test_private_files_cannot_enter_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('worker-secrets.json', 'developer-license.json', 'license.dpapi', 'activation-state.bin', '.env', 'signing.pfx'):
                candidate = root / name
                candidate.touch()
                with self.assertRaises(RuntimeError):
                    release.scan_distribution(root)
                candidate.unlink()
            release.scan_distribution(root)

    def test_signing_and_architecture_must_match_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = ['--qt-prefix', directory, '--openssl-root', directory]
            with patch.object(sys, 'platform', 'darwin'):
                with self.assertRaisesRegex(RuntimeError, 'Notarization requires'):
                    package.parse_args(arguments + ['--notary-profile', 'fixture'])
                with self.assertRaisesRegex(RuntimeError, 'Developer ID'):
                    package.parse_args(arguments + ['--signing-identity', '-'])
                with self.assertRaisesRegex(RuntimeError, 'platform/architecture'):
                    package.parse_args(arguments + ['--architecture', 'x64'])
                self.assertEqual(package.parse_args(arguments + ['--architecture', 'arm64']).channel, 'unsigned')
            with patch.object(sys, 'platform', 'win32'), patch.object(package.shutil, 'which', return_value=None):
                with self.assertRaisesRegex(RuntimeError, 'thumbprint'):
                    package.parse_args(arguments + ['--windows-cert-thumbprint', 'invalid'])

    def test_windows_icon_contains_standard_sizes_with_png_data(self):
        data = (ROOT / 'assets/icons/WorshipCue.ico').read_bytes()
        reserved, kind, count = struct.unpack_from('<HHH', data)
        self.assertEqual((reserved, kind, count), (0, 1, 6))
        sizes = []
        for index in range(count):
            width, height, _, _, planes, bits, length, offset = struct.unpack_from('<BBBBHHII', data, 6 + 16 * index)
            sizes.append(width or 256)
            self.assertEqual(width, height)
            self.assertEqual((planes, bits), (1, 32))
            self.assertLessEqual(offset + length, len(data))
            self.assertEqual(data[offset:offset+8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(sizes, [16,32,48,64,128,256])

    def test_installed_probe_rejects_external_runtime_search_paths(self):
        import json
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'bin').mkdir()
            for name in ('WorshipCue.exe', 'WorshipCueMcp.exe'):
                (root / 'bin' / name).touch()
            legal = root / 'share/WorshipCue/Legal'
            (legal / 'licenses').mkdir(parents=True)
            for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/LGPL-3.0-only.txt'):
                (legal / name).touch()
            base = {'ok':True, 'licenseConfigured':True, 'libraryMigrations':True,
                    'pluginPaths':[str(root / 'bin')], 'qtLibrariesPath':str(root / 'bin')}
            for fields in ({'pluginPaths':['/external/developer/plugins']},
                           {'qtLibrariesPath':'/external/developer/lib'}):
                response = subprocess.CompletedProcess([], 0, json.dumps({**base, **fields}), '')
                with patch.object(release.subprocess, 'run', return_value=response):
                    with self.assertRaisesRegex(RuntimeError, 'outside|external'):
                        release.check_installed(root)


if __name__ == '__main__':
    unittest.main()
