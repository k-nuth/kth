"""Check the production header-index forecast without installing Conan plugins."""

import ast
import fnmatch
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_capacity_forecast():
    # Execute the recipe's actual pure functions and constants. Importing the
    # recipe normally requires Conan and kthbuild, although this calculation
    # needs only datetime. Do not maintain a second copy of the calculation.
    source = ast.parse((ROOT / "conanfile.py").read_text())
    selected = []
    for node in source.body:
        if isinstance(node, ast.ImportFrom) and node.module == "datetime":
            selected.append(node)
        elif isinstance(node, ast.Assign) and all(
            isinstance(target, ast.Name) and target.id.startswith("_")
            for target in node.targets
        ):
            selected.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in {
            "_get_target_year", "_calculate_header_capacity"
        }:
            selected.append(node)
    namespace = {}
    exec(compile(ast.Module(body=selected, type_ignores=[]), "conanfile.py", "exec"), namespace)
    return namespace["_calculate_header_capacity"]


class HeaderCapacityTests(unittest.TestCase):
    def test_conan_exports_every_script_registered_by_root_ctest(self):
        recipe = ast.parse((ROOT / "conanfile.py").read_text())
        exports = next(
            ast.literal_eval(node.value)
            for cls in recipe.body if isinstance(cls, ast.ClassDef) and cls.name == "KthRecipe"
            for node in cls.body if isinstance(node, ast.Assign)
            if any(isinstance(target, ast.Name) and target.id == "exports_sources" for target in node.targets)
        )
        scripts = re.findall(
            r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/(scripts/[^\s)]+\.py)",
            (ROOT / "CMakeLists.txt").read_text(),
        )
        self.assertTrue(scripts, "the CTest script export control must exercise registered scripts")
        # The capacity script reads the production recipe as data, avoiding a
        # second copy of its calculation. Conan does not include it in export_source automatically.
        for script in [*scripts, "conanfile.py"]:
            with self.subTest(script=script):
                self.assertTrue((ROOT / script).is_file())
                self.assertTrue(any(fnmatch.fnmatchcase(script, pattern) for pattern in exports),
                                f"CTest input {script} is absent from Conan exports_sources")

    def test_one_minute_tempnet_fits_through_2030(self):
        # 600-second history from genesis to 2026-10-15 12:00 UTC, then
        # 60-second blocks through 2030-12-31: 3,150,538.475 nominal blocks.
        # The allocation must contain the next whole block and align to 2^16.
        capacity = load_capacity_forecast()(2026)
        self.assertEqual(capacity, 3_211_264)
        self.assertGreaterEqual(capacity, 3_150_539)
        self.assertEqual(capacity % 65_536, 0)

    def test_forecast_keeps_the_existing_five_year_planning_window(self):
        forecast = load_capacity_forecast()
        self.assertEqual(forecast(2029), 3_211_264)
        self.assertEqual(forecast(2030), 5_832_704)
        self.assertEqual(forecast(2035), 8_454_144)

    def test_legacy_currencies_keep_existing_capacity(self):
        forecast = load_capacity_forecast()
        for currency in ("BTC", "LTC"):
            with self.subTest(currency=currency):
                self.assertEqual(forecast(2026, currency), 1_179_648)
                self.assertEqual(forecast(2030, currency), 1_441_792)
                self.assertEqual(forecast(2035, currency), 1_703_936)

    def test_standalone_fallbacks_have_room_for_the_same_horizon(self):
        required = load_capacity_forecast()(2026)
        for path, pattern in (
            ("src/blockchain/CMakeLists.txt", r"set\(KTH_HEADER_INDEX_CAPACITY (\d+)\)"),
            ("src/database/include/kth/database/header_index.hpp", r"#define KTH_HEADER_INDEX_CAPACITY (\d+)"),
        ):
            with self.subTest(path=path):
                found = re.search(pattern, (ROOT / path).read_text())
                self.assertIsNotNone(found)
                self.assertGreaterEqual(int(found.group(1)), required)


if __name__ == "__main__":
    unittest.main()
