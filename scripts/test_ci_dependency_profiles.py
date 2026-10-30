"""Exercise the dependency-planning workflow commands with isolated Conan profiles."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]

class DependencyProfiles(unittest.TestCase):
    def test_planning_matches_host_and_build_profiles(self):
        for workflow, profile in (("calc-deps-with-container.yml", "linux-ci-cd"),
                                  ("calc-deps-without-container.yml", "general-ci-cd")):
            with self.subTest(workflow=workflow), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                home = root / "conan-home"
                (home / "profiles").mkdir(parents=True)
                default = "[settings]\nos=Linux\narch=x86_64\ncompiler=gcc\ncompiler.version=15\ncompiler.libcxx=libstdc++11\ncompiler.cppstd=gnu20\nbuild_type=Release\n"
                (home / "profiles/default").write_text(default)
                (home / "profiles" / profile).write_text("include(default)\n[settings]\ncompiler.cppstd=23\n")
                env = dict(os.environ, CONAN_HOME=str(home), CONAN_NON_INTERACTIVE="1")
                def run(*args):
                    result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True)
                    if result.returncode:
                        raise RuntimeError(result.stderr)
                    return result
                # Both host and build contexts have compiled dependencies. No network or build needed.
                for name in ("profile-host", "profile-tool", "profile-consensus"):
                    recipe = root / name
                    recipe.mkdir()
                    (recipe / "conanfile.py").write_text('from conan import ConanFile\nclass Dep(ConanFile):\n name = "' + name + '"\n version = "1.0"\n settings = "os", "arch", "compiler", "build_type"\n')
                    run("conan", "export", str(recipe))
                (root / "conanfile.py").write_text('from conan import ConanFile\nclass Consumer(ConanFile):\n settings = "os", "arch", "compiler", "build_type"\n options = {"consensus": [True, False]}\n default_options = {"consensus": False}\n tool_requires = "profile-tool/1.0"\n def requirements(self):\n  self.requires("profile-host/1.0")\n  if self.options.consensus:\n   self.requires("profile-consensus/1.0")\n')
                run("conan", "remote", "disable", "*")
                run("conan", "lock", "create", "conanfile.py", "--lockfile-out=conan.lock", "--no-remote", "-o", "consensus=True")
                data = yaml.safe_load((ROOT / ".github/workflows" / workflow).read_text())
                job = next(iter(data["jobs"].values()))
                block = next(s["run"] for s in job["steps"] if "conan graph build-order" in s.get("run", ""))
                commands = [line.strip().replace("${{ inputs.build-version }}", "1.5.0") for line in block.splitlines()
                            if line.strip().startswith("conan ")]
                for command in commands:
                    run("sh", "-ec", command)
                order = json.loads((root / "build_order.json").read_text())
                # The planner must preserve the enabled-only dependencies even
                # though the package defaults to consensus=False.
                self.assertIn("profile-consensus/1.0", {
                    recipe["ref"].split("#")[0]
                    for group in order["order"] for recipe in group
                })
                packages = [pkg for group in order["order"] for recipe in group
                            for batch in recipe["packages"] for pkg in batch]
                self.assertEqual({p["context"] for p in packages}, {"host", "build"})
                for package in packages:
                    self.assertEqual(package["info"]["settings"]["compiler.cppstd"], "23",
                                     (workflow, package["context"], package["info"]))

if __name__ == "__main__":
    unittest.main()
