"""Run the release updater against copied repository READMEs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/update_readme_versions.sh'


class ReleaseReadmes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for source in (ROOT / 'src').glob('*/README.md'):
            target = self.root / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

    def run_update(self, version):
        return subprocess.run(['bash', str(SCRIPT), version], cwd=self.root,
                              capture_output=True, text=True)

    def test_release_preparation_normalizes_install_commands_and_embedded_requirements(self):
        result = self.run_update('9.8.7')
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in ('node-exe', 'consensus', 'database', 'domain', 'infrastructure', 'network', 'node', 'c-api'):
            with self.subTest(component=name):
                text = (self.root / 'src' / name / 'README.md').read_text()
                self.assertIn('--requires=kth/9.8.7', text)
                self.assertNotRegex(text, r'(?:c-api|consensus|database|domain|infrastructure|network|node)/\d+\.\d+\.\d+')
        example = (self.root / 'src/c-api/README.md').read_text()
        self.assertIn(r'[requires]\nkth/9.8.7\n', example)
        self.assertIn('kth/*:shared=True', example)
        consensus = (self.root / 'src/consensus/README.md').read_text()
        self.assertIn("-o 'kth/*:consensus=True'", consensus)

    def test_second_release_updates_canonical_names_without_duplicating_options(self):
        for version in ('9.8.7', '9.8.8'):
            result = self.run_update(version)
            self.assertEqual(result.returncode, 0, result.stderr)
        before = {p: p.read_bytes() for p in (self.root / 'src').glob('*/README.md')}
        self.assertEqual(self.run_update('9.8.8').returncode, 0)
        self.assertEqual(before, {p: p.read_bytes() for p in before})
        for path in (self.root / 'src').glob('*/README.md'):
            text = path.read_text()
            self.assertNotIn('9.8.7', text)
        consensus = (self.root / 'src/consensus/README.md').read_text()
        self.assertEqual(consensus.count("-o 'kth/*:consensus=True'"), 1)

    def test_invalid_version_does_not_change_files(self):
        before = {p: p.read_bytes() for p in (self.root / 'src').glob('*/README.md')}
        self.assertNotEqual(self.run_update('next;echo bad').returncode, 0)
        self.assertEqual(before, {p: p.read_bytes() for p in before})


if __name__ == '__main__':
    unittest.main()
