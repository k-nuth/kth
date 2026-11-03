# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise the recipe's package_info with Conan's real component model."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest

from conan.internal.model.cpp_info import CppInfo


class BrowserPackageInfo(unittest.TestCase):
    def package_info(self, browser, os):
        recipe = Path(__file__).resolve().parents[2] / 'conanfile.py'
        tree = ast.parse(recipe.read_text())
        method = next(n for cls in tree.body if isinstance(cls, ast.ClassDef)
                      for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == 'package_info')
        namespace = {'collect_libs': lambda _: []}
        exec(compile(ast.Module(body=[method], type_ignores=[]), str(recipe), 'exec'), namespace)
        model = SimpleNamespace(
            cpp_info=CppInfo(), is_shared=False, settings=SimpleNamespace(os=os),
            options=SimpleNamespace(currency='BCH', mempool_backend='cfm', rpc=False,
                browser_node=browser, with_png=False, with_qrencode=False,
                with_jemalloc=False, asio_standalone=True, consensus=False))
        namespace['package_info'](model)
        return model.cpp_info.components['infrastructure']

    def test_browser_consumers_receive_compile_and_link_requirements(self):
        info = self.package_info(True, 'Emscripten')
        self.assertIn('-fwasm-exceptions', info.cxxflags)
        for flags in (info.exelinkflags, info.sharedlinkflags):
            self.assertIn('-fwasm-exceptions', flags)
            self.assertIn('-lwebsocket.js', flags)

    def test_native_consumers_do_not_receive_browser_flags(self):
        info = self.package_info(False, 'Linux')
        self.assertNotIn('-fwasm-exceptions', info.cxxflags)
        self.assertNotIn('-lwebsocket.js', info.exelinkflags)

    def test_ordinary_wasm_consumers_do_not_receive_browser_flags(self):
        info = self.package_info(False, 'Emscripten')
        self.assertNotIn('-fwasm-exceptions', info.cxxflags)
        self.assertNotIn('-lwebsocket.js', info.exelinkflags)


if __name__ == '__main__':
    unittest.main()
