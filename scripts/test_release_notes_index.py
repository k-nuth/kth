"""Keep navigation valid through the actual post-release prepend operation."""

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_notes_index', ROOT / 'scripts/update_release_notes_index.py')
INDEX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INDEX)


class ReleaseNotesIndexTests(unittest.TestCase):
    def test_post_release_keeps_index_first_and_adds_new_version(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'scripts').mkdir()
            shutil.copyfile(ROOT / 'scripts/update_release_notes_index.py',
                            root / 'scripts/update_release_notes_index.py')
            notes = root / 'docs/release-notes/release-notes.md'
            notes.parent.mkdir(parents=True)
            notes.write_text(INDEX.update_index('# version 0.1.0\n\nOriginal entry.\n'))
            commands = root / 'commands'
            commands.mkdir()
            # All external GitHub/git/SSH effects are replaced at the process
            # boundary; the release script's file operations execute unchanged.
            stubs = {
                'git': '#!/bin/sh\nif [ "$1" = ls-remote ]; then echo "abc refs/heads/release/9.9.9"; fi\n',
                'gh': '#!/bin/sh\nif [ \"$1 $2\" = \"release view\" ]; then\n  [ \"$3\" = v9.9.9 ] || exit 1\n  case \"$*\" in *publishedAt*) printf \"2020-02-11\\n\";; *) printf \"## What changed\\n\\nNew release.\\n\";; esac\nfi\n',
                'ssh-add': '#!/bin/sh\nexit 0\n',
                'date': '#!/bin/sh\nexit 77\n',
            }
            for name, text in stubs.items():
                target = commands / name
                target.write_text(text)
                target.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                       SSH_AUTH_SOCK='isolated-release-control')
            result = subprocess.run(['bash', str(ROOT / 'scripts/post-release.sh'), '9.9.9'],
                                    cwd=root, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            text = notes.read_text()
            self.assertTrue(text.startswith(INDEX.START + '\n# Release notes\n'), text[:300])
            self.assertEqual(text.count('[9.9.9](#version-999)'), 1)
            self.assertIn('## version 9.9.9\n', text)
            self.assertIn('### What changed\n', text)
            self.assertIn('**Published:** [2020-02-11](https://github.com/k-nuth/kth/releases/tag/v9.9.9)', text)
            self.assertNotIn('You can install', text)
            self.assertIn('Original entry.', text)

    def test_real_document_has_one_link_per_version_and_is_idempotent(self):
        text = (ROOT / 'docs/release-notes/release-notes.md').read_text()
        formatted = INDEX.update_index(text)
        self.assertEqual(INDEX.update_index(formatted), formatted)
        headings = re.findall(r'^## version (\d+\.\d+\.\d+)$', formatted, re.M)
        self.assertGreater(len(headings), 80)
        for version in headings:
            self.assertEqual(formatted.count(f'[{version}](#version-{version.replace(".", "")})'), 1)

    def test_real_document_has_dates_and_one_versioning_explanation(self):
        from datetime import date
        text = (ROOT / 'docs/release-notes/release-notes.md').read_text()
        entries = re.findall(r'^## version (\d+\.\d+\.\d+)\n\n\*\*Published:\*\* \[(\d{4}-\d{2}-\d{2})\]\((https://github.com/k-nuth/[^)]+)\)', text, re.M)
        headings = re.findall(r'^## version ', text, re.M)
        self.assertEqual(len(entries), len(headings))
        for version, published, url in entries:
            date.fromisoformat(published)
            self.assertTrue(url.endswith('/releases/tag/v' + version))
        self.assertEqual(text.count('## Versioning History Note'), 1)
        self.assertNotIn('What Happened During', text)
        self.assertEqual(text.count('### New Contributors'), 1)
        self.assertIn('@CyberAshven made their first contribution', text)
        self.assertNotIn('@fpelliccioni made their first contribution', text)
        for removed in ('Full Changelog', 'can install Knuth', 'flipstarter'):
            self.assertNotIn(removed, text)

    def test_document_cleanup_removes_release_page_boilerplate(self):
        text = (
            '# version 1.0.0\n\n'
            'You can install Knuth node version 1.0.0 [using these instructions](https://kth.cash/#download).\n\n'
            '## Changes\n\n- Keep this actual change.\n\n'
            '**Full Changelog**: https://github.com/k-nuth/kth/compare/v0.1.0...v1.0.0\n\n'
            '## New Contributors\n\n- @example made their first contribution.\n\n'
            '## Important notes\n\nKeep this note.\n'
        )
        result = INDEX.update_index(text)
        self.assertNotIn('You can install', result)
        self.assertNotIn('Full Changelog', result)
        self.assertIn('New Contributors', result)
        self.assertIn('@example', result)
        self.assertIn('- Keep this actual change.', result)
        self.assertIn('Keep this note.', result)

    def test_only_monorepo_bootstrap_credit_is_removed(self):
        text = ('# version 1.1.0\n\n## New Contributors\n'
                '* @CyberAshven made their first contribution.\n\n'
                '# version 0.68.0\n\n## New Contributors\n'
                '* @fpelliccioni made their first contribution.\n')
        result = INDEX.update_index(text)
        self.assertIn('@CyberAshven made their first contribution', result)
        self.assertNotIn('@fpelliccioni made their first contribution', result)

    def test_fenced_code_and_release_text_are_preserved(self):
        text = '# version 1.0.0\n\n## Changes\n\n```sh\n# version 9.9.9\n## shell comment\n```\n\nOriginal text.\n'
        result = INDEX.update_index(text)
        self.assertIn('### Changes\n', result)
        self.assertIn('```sh\n# version 9.9.9\n## shell comment\n```', result)
        self.assertNotIn('[9.9.9]', result)
        self.assertIn('Original text.', result)

    def test_duplicate_versions_fail_without_overwriting_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'notes.md'
            original = '# version 1.0.0\n\nFirst.\n\n# version 1.0.0\n\nDuplicate.\n'
            path.write_text(original)
            result = subprocess.run(['python3', str(ROOT / 'scripts/update_release_notes_index.py'), str(path)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Duplicate release version', result.stderr)
            self.assertEqual(path.read_text(), original)


if __name__ == '__main__':
    unittest.main()
