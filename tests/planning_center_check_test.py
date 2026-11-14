"""Exercise the live checker without account credentials or network access."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHECKER = Path(__file__).resolve().parents[1] / "tools/check-planning-center.py"
FAKE = '''#!{python}
import json, os, pathlib, sys
database = pathlib.Path(sys.argv[2])
assert database.parent.name.startswith("worshipcue-planning-check-")
pathlib.Path(os.environ["CHECK_DATABASE_LOG"]).write_text(str(database))
database.write_text("isolated fixture")
for line in sys.stdin:
    request = json.loads(line)
    if "id" not in request: continue
    if request["method"] == "initialize": result = {{"protocolVersion":"2025-11-25"}}
    elif request["method"] == "tools/list":
        result = {{"tools":[{{"name": name}} for name in ["list_planning_center_service_types", "list_planning_center_plans", "get_planning_center_plan", "import_planning_center_service"]]}}
    else:
        params = request["params"]
        if params["name"] == "list_planning_center_service_types":
            result = {{"structuredContent":{{"data":[]}}}}
        else:
            assert params["name"] == "import_planning_center_service"
            assert params["arguments"]["dry_run"] is True
            ready = os.environ.get("CHECK_READY") == "yes"
            result = {{"structuredContent":{{"dry_run":True, "saved":False, "ready_to_save":ready,
                "source_items":[{{"resolution":"imported_arrangement_lyrics"}}],
                "unresolved_items":[] if ready else [{{"item_id":"30","reason":"Import Lyrics into Lyrics & Chords."}}]}}}}
    print(json.dumps({{"jsonrpc":"2.0","id":request["id"],"result":result}}))
'''


class PlanningCenterCheckTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.companion = self.root / "fixture-companion"
        self.companion.write_text(FAKE.format(python=sys.executable))
        self.companion.chmod(0o700)
        self.environment = {key: value for key, value in os.environ.items() if not key.startswith("PLANNING_CENTER_")}
        self.environment.update(PLANNING_CENTER_ACCESS_TOKEN="fixture-token-do-not-print", CHECK_READY="yes",
                                CHECK_DATABASE_LOG=str(self.root / "database-path"))

    def run_check(self, *arguments):
        result = subprocess.run([sys.executable, str(CHECKER), "--executable", str(self.companion), *arguments],
                                env=self.environment, text=True, capture_output=True, timeout=10)
        self.assertNotIn("fixture-token-do-not-print", result.stdout + result.stderr)
        log = self.root / "database-path"
        if log.exists():
            self.assertFalse(Path(log.read_text()).exists(), "Temporary library survived the checker")
        return result

    def test_connection_only(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("authentication, Services access", result.stdout)

    def test_plan_is_previewed_without_saving(self):
        result = self.run_check("--plan-id", "20", "--service-type-id", "10")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("No service was saved", result.stdout)

    def test_missing_lyrics_explain_resolution(self):
        self.environment["CHECK_READY"] = "no"
        result = self.run_check("--plan-url", "https://services.planningcenteronline.com/plans/20")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Item 30: Import Lyrics into Lyrics & Chords", result.stdout)

    def test_missing_credentials_never_start_companion(self):
        del self.environment["PLANNING_CENTER_ACCESS_TOKEN"]
        result = self.run_check()
        self.assertEqual(result.returncode, 2)
        self.assertIn("not configured", result.stderr)
        self.assertFalse((self.root / "database-path").exists())


if __name__ == "__main__":
    unittest.main()
