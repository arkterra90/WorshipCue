#!/bin/bash
# Double-click in Finder to configure and rebuild WorshipCue on macOS.
# All project paths come from this file's location, so the project can move.
set -euo pipefail

# Finder's Terminal sessions may not include Homebrew in PATH.
export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}"

finish() {
    local build_status=$?
    trap - EXIT
    if [ "$build_status" -ne 0 ]; then
        printf '\nRebuild failed (exit %s). See the error above.\n' "$build_status"
    fi
    if [ -t 0 ]; then
        printf '\nPress Return to close this window... '
        read -r reply || true
    fi
    exit "$build_status"
}
trap finish EXIT

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
build_dir="$project_dir/build-local"
cd "$project_dir"

printf '\nRebuilding WorshipCue\nProject: %s\n\n' "$project_dir"
if [ ! -f "$project_dir/CMakeLists.txt" ]; then
    printf 'Keep this file in the WorshipCue folder beside CMakeLists.txt.\n' >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    printf 'CMake is missing. Install the build tools with: brew install cmake qt\n' >&2
    exit 1
fi

# CMake caches absolute paths. Preserve an old build if the project was moved.
if [ -f "$build_dir/CMakeCache.txt" ]; then
    cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$build_dir/CMakeCache.txt")"
    cached_build="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$build_dir/CMakeCache.txt")"
    if [ "$cached_source" != "$project_dir" ] || [ "$cached_build" != "$build_dir" ]; then
        previous_build="$(mktemp -d "$project_dir/build-local.previous.XXXXXX")"
        mv "$build_dir" "$previous_build/build-local"
        printf 'Project location changed. Previous build kept in:\n%s\n\n' "$previous_build"
    fi
fi

configure_args=(-S "$project_dir" -B "$build_dir")
# Respect an explicitly supplied Qt search path; otherwise discover Homebrew Qt.
if [ -z "${CMAKE_PREFIX_PATH:-}" ]; then
    for qt_prefix in /opt/homebrew/opt/qt /opt/homebrew/opt/qt@6 /usr/local/opt/qt /usr/local/opt/qt@6; do
        if [ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]; then
            configure_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
            break
        fi
    done
fi

cmake "${configure_args[@]}"
cmake --build "$build_dir" --target WorshipCue --parallel 4

printf '\nRebuild complete.\nApp: %s\nQuit and reopen WorshipCue from this build to use the updates.\n' \
    "$build_dir/WorshipCue.app"
