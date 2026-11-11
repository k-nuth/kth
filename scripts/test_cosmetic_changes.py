"""Exercise the cosmetic workflow's real path classifier."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CosmeticChangesTests(unittest.TestCase):
    def test_build_files_are_not_cosmetic_text(self):
        workflow = (ROOT / '.github/workflows/cosmetic-changes.yml').read_text()
        classifier = workflow.split('          ALLOWED_PATTERNS=(', 1)[1]
        classifier = 'ALLOWED_PATTERNS=(' + classifier.split('          # Verificar todos los archivos cambiados', 1)[0]
        for path, allowed in [('README.md', True), ('docs/guide.txt', True),
                              ('CMakeLists.txt', False), ('src/c-api/CMakeLists.txt', False),
                              ('docs/example/CMakeLists.txt', False), ('src/node.cpp', False)]:
            with self.subTest(path=path):
                result = subprocess.run(['bash', '-c', classifier + '\nis_cosmetic_file "$1"', 'control', path],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if allowed else 1, result.stderr)


if __name__ == '__main__':
    unittest.main()
