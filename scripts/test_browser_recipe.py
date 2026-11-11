"""The Conan recipe's browser node, with the real Conan option and component
models; only kthbuild's unrelated helpers are stubbed."""

from pathlib import Path
import runpy
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

from conan import ConanFile

ROOT = Path(__file__).resolve().parents[1]


class BuildHelpers(ConanFile):
    def config_options(self):
        pass

    def configure(self):
        pass


def recipe(system, browser):
    helpers = SimpleNamespace(KnuthConanFileV2=BuildHelpers,
                              option_on_off=lambda value: "ON" if value else "OFF")
    with mock.patch.dict(sys.modules, kthbuild=helpers):
        module = runpy.run_path(str(ROOT / "conanfile.py"))
    result = module["KthRecipe"]()
    result.settings = SimpleNamespace(os=system)
    result.cpp_info = result.cpp.package
    result.config_options()
    result.options.rpc = "none"
    result.options.browser_node = browser
    result.configure()
    return result


class BrowserRecipeTests(unittest.TestCase):
    def test_browser_node_exports_its_link_requirements(self):
        # A consumer of the installed package compiles and links with
        # WebAssembly exceptions and Emscripten's WebSocket API, as the package
        # itself was built, without adding them by hand.
        browser = recipe("Emscripten", True)
        browser.package_info()
        infrastructure = browser.cpp_info.components["infrastructure"]
        network = browser.cpp_info.components["network"]
        self.assertIn("-fwasm-exceptions", infrastructure.cxxflags)
        self.assertIn("-fwasm-exceptions", infrastructure.exelinkflags)
        self.assertIn("-lwebsocket.js", network.exelinkflags)

    def test_native_node_exports_no_browser_flags(self):
        native = recipe("Linux", False)
        native.package_info()
        self.assertNotIn("-fwasm-exceptions", native.cpp_info.components["infrastructure"].cxxflags)
        self.assertNotIn("-lwebsocket.js", native.cpp_info.components["network"].exelinkflags)


    def test_browser_node_names_no_ftxui(self):
        # FTXUI is only required natively: a browser node's component must not
        # name it, or a consumer's Conan graph does not resolve.
        browser = recipe("Emscripten", True)
        browser.requirements()
        self.assertNotIn("ftxui", {r.ref.name for r in browser.requires.values()})
        browser.package_info()
        node = browser.cpp_info.components["node"].requires
        self.assertIn("network", node)
        self.assertNotIn("ftxui::ftxui", node)

    def test_native_node_keeps_ftxui(self):
        native = recipe("Linux", False)
        native.requirements()
        self.assertIn("ftxui", {r.ref.name for r in native.requires.values()})
        native.package_info()
        self.assertIn("ftxui::ftxui", native.cpp_info.components["node"].requires)


if __name__ == "__main__":
    unittest.main()
