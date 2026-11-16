"""Configure the actual README compile target with LF and CRLF checkouts."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CApiReadmeTests(unittest.TestCase):
    def test_actual_cmake_extraction_accepts_checkout_line_endings(self):
        cmake = (ROOT / 'src/c-api/CMakeLists.txt').read_text()
        block = cmake.split('    # Compile and link the component README itself, rather than a separate copy.\n', 1)[1].split('    # Chain-interface', 1)[0]
        readme = (ROOT / 'src/c-api/README.md').read_text()
        expected = re.search(r'```c\n(.*?)```', readme, re.S)[1]
        for newline in ('\n', '\r\n'):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                (root / 'README.md').write_bytes(readme.replace('\n', newline).encode())
                (root / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.23)\nproject(c-api LANGUAGES C)\n'
                    'add_library(c-api INTERFACE)\n' + block)
                result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build')],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((root / 'build/component_readme.c').read_bytes(), expected.encode())


if __name__ == '__main__':
    unittest.main()
