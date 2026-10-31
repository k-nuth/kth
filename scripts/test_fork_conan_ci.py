"""Exercise Conan authentication/upload gates with real workflow steps."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]


def enabled(expression, event):
    """Evaluate the small boolean expressions used by the selected steps."""
    expression = str(expression or "true").removeprefix("${{").removesuffix("}}").strip()
    values = {
        "github.event_name": event["name"],
        "github.event.pull_request.head.repo.full_name": event["head"],
        "github.repository": "k-nuth/kth",
        "inputs.reference": "ci-fixture/1.0",
    }
    for key in sorted(values, key=len, reverse=True):
        expression = expression.replace(key, repr(values[key]))
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"\btrue\b", "True", expression)
    expression = re.sub(r"\bfalse\b", "False", expression)
    return eval(expression, {"__builtins__": {}}, {})


class ForkConanCI(unittest.TestCase):
    def test_external_pr_skips_auth_and_upload_but_keeps_builds(self):
        for name in ("calc-deps-with-container.yml", "calc-deps-without-container.yml",
                     "build-deps-with-container.yml", "build-deps-without-container.yml",
                     "build-wasm.yml"):
            data = yaml.safe_load((ROOT / ".github/workflows" / name).read_text())
            steps = [step for job in data["jobs"].values() for step in job.get("steps", [])]
            login = next(s for s in steps if "conan remote login" in s.get("run", ""))
            builds = [s for s in steps if any(command in s.get("run", "") for command in
                      ("conan graph build-order", "conan install --requires", "conan create"))]
            self.assertTrue(builds, name)
            for event_name, head, trusted in (("pull_request", "contributor/kth", False),
                                             ("pull_request", "k-nuth/kth", True),
                                             ("push", "", True),
                                             ("workflow_dispatch", "", True)):
                event = {"name": event_name, "head": head}
                with self.subTest(workflow=name, event=event):
                    self.assertEqual(enabled(login.get("if"), event), trusted)
                    for step in builds:
                        self.assertTrue(enabled(step.get("if"), event), step["name"])
                    if name.startswith("build-deps-"):
                        upload = next(s for s in steps if "conan upload" in s.get("run", ""))
                        self.assertEqual(enabled(upload.get("if"), event), trusted)
                    if name == "build-wasm.yml":
                        deps = next(s for s in steps if "done < deps_list.txt" in s.get("run", ""))
                        allowed = enabled(deps.get("env", {}).get("ALLOW_CONAN_UPLOAD"), event)
                        self.assertEqual(allowed, trusted)
                        self.run_dependency_loop(deps["run"], allowed)

    def run_dependency_loop(self, block, allowed):
        # Execute the actual workflow loop with a recording Conan CLI. A missing
        # gate must attempt upload and fail the external-PR control.
        loop = block[block.index("while IFS="):]
        loop = loop.replace("${{ env.CONAN_REMOTE }}", "kth")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "deps_list.txt").write_text("ci-fixture/1.0|host\nemsdk/6.0.13|build\nninja/1.13.1|build\n")
            conan = root / "conan"
            conan.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$CONAN_CALLS"\n'
                             'if [ "$1" = upload ] && [ "$ALLOW_CONAN_UPLOAD" != true ]; then exit 91; fi\n')
            conan.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"],
                       CONAN_CALLS=str(root / "calls"), ALLOW_CONAN_UPLOAD=str(allowed).lower())
            result = subprocess.run(["bash", "-eu", "-c", loop], cwd=root, env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = (root / "calls").read_text().splitlines()
            self.assertEqual(sum(c.startswith("install ") for c in calls), 3)
            self.assertEqual(sum(c.startswith("upload ") for c in calls), int(allowed))


if __name__ == "__main__":
    unittest.main()
