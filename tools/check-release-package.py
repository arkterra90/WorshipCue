#!/usr/bin/env python3
"""Validate an installed/relocated app using only its bundled runtime."""
import argparse
import json
from pathlib import Path
import sys
from release_support import check_installed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installation', type=Path, help='.app on macOS, install root on Windows')
    parser.add_argument('--media-fixture', type=Path)
    parser.add_argument('--architecture')
    args = parser.parse_args()
    print(json.dumps(check_installed(args.installation.resolve(strict=True), args.media_fixture, args.architecture), indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
