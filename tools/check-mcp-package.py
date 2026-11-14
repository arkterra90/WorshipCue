#!/usr/bin/env python3
"""Smoke-test an installed MCP companion with no developer Qt environment.

Uses only a temporary library, fixture media, home, and working directory.
This verifies MCP deployment, not signing, installers, or a clean OS image.
"""
import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_runtime(executable):
    if sys.platform == "darwin":
        app = next((p for p in executable.parents if p.suffix == ".app"), None)
        require(app is not None, "Choose the companion inside the installed .app bundle.")
        contents = app / "Contents"
        require((contents / "Frameworks/QtCore.framework").is_dir(), "Deploy Qt into the installed bundle first.")
        require(list(contents.glob("PlugIns/sqldrivers/*sqlite*")), "Missing deployed SQLite driver.")
        require(list(contents.glob("PlugIns/tls/*")), "Missing deployed Network TLS backend.")
        # Follow the companion + SQL/TLS dependency graph, including third-party
        # libraries. GUI-only deployment belongs to the distribution checks.
        binaries = [executable, *contents.glob("PlugIns/sqldrivers/*sqlite*"), *contents.glob("PlugIns/tls/*.dylib")]
        checked = set()
        while binaries:
            binary = binaries.pop().resolve()
            if binary in checked:
                continue
            checked.add(binary)
            require(binary.is_file(), f"Missing deployed dependency: {binary}")
            dependencies = subprocess.check_output(["/usr/bin/otool", "-L", str(binary)], text=True)
            # A dylib/framework's first entry is its own install ID, not a load.
            start = 2 if binary.suffix == ".dylib" or ".framework/" in str(binary) else 1
            for line in dependencies.splitlines()[start:]:
                dependency = line.strip().split(" (", 1)[0]
                if dependency.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                if dependency.startswith("@rpath/"):
                    target = contents / "Frameworks" / dependency.removeprefix("@rpath/")
                elif dependency.startswith("@loader_path/"):
                    target = binary.parent / dependency.removeprefix("@loader_path/")
                elif dependency.startswith("@executable_path/"):
                    target = executable.parent / dependency.removeprefix("@executable_path/")
                else:
                    raise RuntimeError(f"Undeployed dependency in {binary.name}: {dependency}")
                require(target.resolve().is_relative_to(contents.resolve()), f"Dependency escapes installed bundle: {dependency}")
                binaries.append(target)
    elif sys.platform == "win32":
        root = executable.parent
        require((root / "Qt6Core.dll").is_file(), "Deploy Qt DLLs beside the companion first.")
        require((root / "sqldrivers/qsqlite.dll").is_file(), "Missing deployed SQLite driver.")
        require(list((root / "tls").glob("*.dll")), "Missing deployed Network TLS backend.")


def smoke(executable, root):
    database = root / "isolated library.sqlite3"
    environment = {key: value for key, value in os.environ.items()
                   if key in {"SystemRoot", "WINDIR", "COMSPEC"}}
    environment.update(HOME=str(root), USERPROFILE=str(root), TMPDIR=str(root), TEMP=str(root), TMP=str(root),
                       PATH=os.pathsep.join([os.path.dirname(sys.executable), os.defpath]))

    def run(calls):
        requests = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "WorshipCue package check", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"}]
        requests += [{"jsonrpc": "2.0", "id": index + 2, "method": method, "params": params}
                     for index, (method, params) in enumerate(calls)]
        completed = subprocess.run([str(executable), "--database", str(database)], cwd=root, env=environment,
                                   input="".join(json.dumps(r) + "\n" for r in requests),
                                   text=True, capture_output=True, timeout=30)
        require(completed.returncode == 0, f"Companion startup/exit failed ({completed.returncode}); check deployed Qt and SQL dependencies.")
        responses = [json.loads(line) for line in completed.stdout.splitlines()]
        require(len(responses) == len(calls) + 1, "Unexpected stdout framing or missing MCP responses.")
        for index, response in enumerate(responses):
            require(response.get("id") == index + 1 and "error" not in response, "Invalid JSON-RPC response.")
        require(responses[0]["result"].get("protocolVersion") == "2025-11-25", "MCP negotiation failed.")
        return [response["result"] for response in responses[1:]]

    def call(name, arguments=None):
        return "tools/call", {"name": name, "arguments": arguments or {}}

    def data(response):
        require(not response.get("isError"), f"Tool failed: {response.get('structuredContent', {}).get('error')}")
        return response["structuredContent"]

    listing, created = run([("tools/list", {}), call("create_song_presentation", {
        "title": "Package fixture", "lyrics": "[Verse]\nOne verse\n\n[Chorus]\nOne chorus"})])
    names = {tool["name"] for tool in listing["tools"]}
    require({"list_media_assets", "get_media_asset", "update_song_section", "save_song_arrangement"} <= names,
            "The packaged companion is out of date.")
    song_id = data(created)["song_id"]
    # Fixture setup only: external media import is deliberately outside the MCP API.
    (root / "media").mkdir()
    (root / "media/fixture.png").write_bytes(b"isolated package fixture")
    with sqlite3.connect(database) as connection:
        asset_id = connection.execute("INSERT INTO media_assets(name,kind,path,loop,imported_at) VALUES(?,?,?,?,?)",
                                      ("Package media", "image", "media/fixture.png", 0, 0)).lastrowid
    media, inspected, order, invalid = run([
        call("list_media_assets"), call("get_presentation", {"song_id": song_id}),
        call("create_service_order", {"name": "Package service", "items": [
            {"song_id": song_id}, {"media_asset_id": asset_id}, {"media_asset_id": asset_id}]}),
        call("get_media_asset", {"media_asset_id": -1})])
    require(data(media)["media_assets"][0]["media_asset_id"] == asset_id, "Media discovery failed.")
    presentation = data(inspected)
    service_id = data(order)["service_id"]
    require(invalid.get("isError"), "Invalid tool input was accepted.")
    sections = presentation["song_structure"]["sections"]
    edited, refreshed = run([call("update_song_section", {"song_id": song_id,
        "expected_revision": presentation["revision"], "section_id": sections[-1]["id"],
        "slides": ["Updated shared chorus"], "dry_run": False}), call("get_presentation", {"song_id": song_id})])
    require(data(edited)["saved"], "Section edit did not save.")
    require(data(refreshed)["slides"][-1]["text"] == "Updated shared chorus", "Section persistence failed.")
    arranged, service = run([call("save_song_arrangement", {"song_id": song_id,
        "expected_revision": data(refreshed)["revision"], "name": "Package arrangement",
        "section_ids": [sections[-1]["id"], sections[0]["id"], sections[-1]["id"]],
        "make_active": True, "dry_run": False}), call("get_service", {"service_id": service_id})])
    require(data(arranged)["saved"], "Arrangement edit did not save.")
    entries = data(service)["items"]
    require(len(entries) == 3 and entries[0]["slide_count"] == 3 and entries[1]["kind"] == "media",
            "Mixed service/arrangement persistence failed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="Installed WorshipCueMcp executable")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    check_runtime(executable)
    with tempfile.TemporaryDirectory(prefix="worshipcue-mcp-package-") as temporary:
        smoke(executable, Path(temporary))
    print("PASS: installed MCP startup, protocol, discovery, media, mixed services, shared edits, validation and persistence.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
