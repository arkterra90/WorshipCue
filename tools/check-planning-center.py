#!/usr/bin/env python3
"""Verify Planning Center access and optional plan import through the real MCP companion.

Credentials come only from the environment. Uses a temporary library and dry-run
imports; does not modify Planning Center or the operator's WorshipCue library.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path, help="Path to WorshipCueMcp")
    plan = parser.add_mutually_exclusive_group()
    plan.add_argument("--plan-url", help="Planning Center Services browser plan URL")
    plan.add_argument("--plan-id", help="Numeric Planning Center plan ID")
    parser.add_argument("--service-type-id", help="Service type ID; use with --plan-id for direct lookup")
    options = parser.parse_args()
    if options.service_type_id and not options.plan_id:
        parser.error("--service-type-id requires --plan-id")
    executable = options.executable.expanduser().resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        parser.error("Choose an executable WorshipCueMcp companion.")
    if not os.environ.get("PLANNING_CENTER_ACCESS_TOKEN") and not (
        os.environ.get("PLANNING_CENTER_APPLICATION_ID") and os.environ.get("PLANNING_CENTER_SECRET")
    ):
        print("Planning Center credentials are not configured in this process's environment.", file=sys.stderr)
        print("Use your church's PLANNING_CENTER_APPLICATION_ID / PLANNING_CENTER_SECRET, or an OAuth PLANNING_CENTER_ACCESS_TOKEN.", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="worshipcue-planning-check-") as temporary:
        database = str(Path(temporary) / "isolated.sqlite3")

        def run(calls):
            messages = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-11-25", "capabilities": {},
                "clientInfo": {"name": "WorshipCue Planning Center check", "version": "1"}}},
                {"jsonrpc": "2.0", "method": "notifications/initialized"}]
            messages += [{"jsonrpc": "2.0", "id": index + 2, "method": method, "params": params}
                         for index, (method, params) in enumerate(calls)]
            completed = subprocess.run([str(executable), "--database", database], cwd=temporary,
                                       input="".join(json.dumps(message) + "\n" for message in messages),
                                       text=True, capture_output=True, timeout=180)
            if completed.returncode:
                raise RuntimeError("Companion startup failed. Check Qt/SQLite/TLS deployment.")
            try:
                responses = [json.loads(line) for line in completed.stdout.splitlines()]
            except ValueError as error:
                raise RuntimeError("Companion returned invalid MCP framing.") from error
            if len(responses) != len(calls) + 1:
                raise RuntimeError("Companion returned incomplete MCP responses.")
            for index, response in enumerate(responses):
                if response.get("id") != index + 1 or "error" in response or not isinstance(response.get("result"), dict):
                    raise RuntimeError("Companion rejected an MCP request; rebuild the current companion.")
            if responses[0]["result"].get("protocolVersion") != "2025-11-25":
                raise RuntimeError("Companion protocol negotiation failed.")
            return [response["result"] for response in responses[1:]]

        def call(name, arguments=None):
            return "tools/call", {"name": name, "arguments": arguments or {}}

        def data(response):
            payload = response.get("structuredContent", {})
            if response.get("isError"):
                raise RuntimeError(payload.get("error", "Planning Center operation failed. Review the plan in your MCP client."))
            return payload

        listing, types = run([("tools/list", {}), call("list_planning_center_service_types", {"limit": 1})])
        required = {"list_planning_center_service_types", "list_planning_center_plans",
                    "get_planning_center_plan", "import_planning_center_service"}
        if not required <= {tool["name"] for tool in listing.get("tools", [])}:
            raise RuntimeError("Companion is missing Planning Center tools; rebuild it.")
        if not isinstance(data(types).get("data"), list):
            raise RuntimeError("Planning Center returned an invalid service type collection.")
        print("Planning Center authentication, Services access, and MCP tools verified.")
        arguments = {}
        if options.plan_url:
            arguments["plan_url"] = options.plan_url
        elif options.plan_id:
            arguments["plan_id"] = options.plan_id
            if options.service_type_id:
                arguments["service_type_id"] = options.service_type_id
        else:
            print("Add --plan-url or --plan-id to verify a plan's lyrics and import readiness.")
            return 0
        preview = data(run([call("import_planning_center_service", {**arguments, "dry_run": True})])[0])
        if preview.get("saved") or preview.get("dry_run") is not True:
            raise RuntimeError("The import did not confirm preview-only operation.")
        sources = preview.get("source_items", [])
        songs = sum(item.get("resolution") == "imported_arrangement_lyrics" for item in sources)
        issues = preview.get("unresolved_items", [])
        print(f"Plan preview: {len(sources)} items; {songs} songs with importable arrangement lyrics; {len(issues)} unresolved items.")
        for issue in issues[:50]:
            print(f"Item {issue.get('item_id')}: {issue.get('reason')}")
        if not preview.get("ready_to_save"):
            print("Resolve the flagged items in Planning Center or with MCP item overrides, then preview again.")
            return 1
        print("Plan is ready to import. No service was saved and no Planning Center data was changed.")
        return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        if isinstance(error, subprocess.TimeoutExpired):
            print("Planning Center check timed out. Check connectivity or supply explicit service type and plan IDs.", file=sys.stderr)
        elif isinstance(error, OSError):
            print("Could not start the companion or create the temporary check library.", file=sys.stderr)
        else:
            print(str(error), file=sys.stderr)
        sys.exit(1)
