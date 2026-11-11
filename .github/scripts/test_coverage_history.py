import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('history', Path(__file__).with_name('coverage-history.py'))
history = importlib.util.module_from_spec(spec)
spec.loader.exec_module(history)


class HistoryTests(unittest.TestCase):
    def result(self, sha='a', source=None, hits=None):
        hits = hits or {'1': 1, '2': 0}
        return {'sha': sha, 'covered': sum(v > 0 for v in hits.values()), 'total': len(hits),
                'files': {'src/a.cpp': {'source': source or ['one', 'two'], 'hits': hits}}}

    def test_nightly_skips_only_the_last_measured_commit(self):
        with patch.object(history, 'latest_baseline', return_value=(7, self.result())):
            self.assertEqual(history.plan({}, 'schedule', 'a')['run'], 'false')
            self.assertEqual(history.plan({}, 'schedule', 'b')['run'], 'true')
            # Age is irrelevant: missed nights and failed builds stay pending.
            self.assertEqual(history.plan({}, 'schedule', 'old-unmeasured')['run'], 'true')
            self.assertEqual(history.plan({}, 'workflow_dispatch', 'a')['run'], 'true')

    def test_first_run_has_no_baseline(self):
        with patch.object(history, 'latest_baseline', return_value=(0, None)):
            self.assertEqual(history.plan({}, 'schedule', 'a')['run'], 'true')

    def test_pull_request_measurement_is_explicitly_requested(self):
        workflow = Path(__file__).parents[1] / 'workflows/coverage.yml'
        text = workflow.read_text()
        self.assertNotIn('  push:', text)
        self.assertIn("contains(github.event.pull_request.labels.*.name, 'coverage')", text)
        self.assertIn('timezone: Europe/Madrid', text)
        self.assertNotIn('pull_request_target', text)
        self.assertNotIn('continue-on-error: true\n    # Ceiling', text)

    def test_actual_plan_gate_ignores_unrelated_label_events(self):
        workflow = Path(__file__).parents[1] / 'workflows/coverage.yml'
        plan = workflow.read_text().split('  plan:\n', 1)[1].split('    runs-on:', 1)[0]
        expression = next(line.split('if:', 1)[1].strip() for line in plan.splitlines() if 'if:' in line)
        cases = [
            ('pull_request', 'labeled', 'bug', ['coverage', 'bug'], False),
            ('pull_request', 'labeled', 'coverage', ['coverage'], True),
            ('pull_request', 'synchronize', '', ['coverage'], True),
            ('pull_request', 'opened', '', [], False),
            ('pull_request', 'labeled', 'bug', ['bug'], False),
            ('schedule', '', '', [], True),
            ('workflow_dispatch', '', '', [], True),
        ]
        for event, action, label, labels, expected in cases:
            with self.subTest(event=event, action=action, label=label):
                actual = expression.replace("contains(github.event.pull_request.labels.*.name, 'coverage')", repr('coverage' in labels))
                for key, value in [('github.event_name', event), ('github.event.action', action), ('github.event.label.name', label)]:
                    actual = actual.replace(key, repr(value))
                actual = actual.replace('||', ' or ').replace('&&', ' and ')
                self.assertEqual(eval(actual, {'__builtins__': {}}, {}), expected)

    def test_baselines_exclude_prs_forks_and_other_workflows(self):
        run = {'path': '.github/workflows/coverage.yml', 'event': 'schedule',
               'head_branch': 'master', 'head_repository': {'full_name': 'org/repo'}}
        with patch.dict(os.environ, GITHUB_REPOSITORY='org/repo'):
            self.assertTrue(history.trusted_run(run))
            for changes in ({'event': 'pull_request'}, {'head_branch': 'feature'},
                            {'path': '.github/workflows/other.yml'},
                            {'head_repository': {'full_name': 'fork/repo'}}):
                self.assertFalse(history.trusted_run({**run, **changes}))

    def test_line_insertions_are_not_losses(self):
        before = self.result(hits={'1': 1, '2': 1})
        after = self.result('b', ['new', 'one', 'two'], {'1': 0, '2': 1, '3': 1})
        title, rows = history.compare(before, after)
        self.assertIn('100.000% → 66.667%', title)
        self.assertIn('now uncovered: []', rows[0])

    def test_unchanged_line_losing_coverage_is_reported_at_its_new_number(self):
        before = self.result(hits={'1': 1, '2': 1})
        after = self.result('b', ['new', 'one', 'two'], {'1': 1, '2': 1, '3': 0})
        self.assertIn('now uncovered: [3]', history.compare(before, after)[1][0])

    def test_scope_changes_are_visible_even_when_total_rate_increases(self):
        before = self.result()
        after = self.result('b', hits={'1': 1, '2': 1})
        after['files'] = {'src/other.cpp': after['files']['src/a.cpp']}
        rows = history.compare(before, after)[1]
        self.assertTrue(any('removed from measured scope' in row for row in rows))
        self.assertTrue(any('added to measured scope' in row for row in rows))

    def test_lcov_aggregates_duplicate_translation_units_and_rejects_negative_counts(self):
        with tempfile.TemporaryDirectory() as root:
            old = os.getcwd()
            os.chdir(root)
            self.addCleanup(os.chdir, old)
            Path('a.cpp').write_text('one\ntwo\n')
            Path('coverage.info').write_text('SF:a.cpp\nDA:1,0\nDA:2,0\nend_of_record\nSF:a.cpp\nDA:1,2\nend_of_record\n')
            result = history.read_lcov('coverage.info', 'a')
            self.assertEqual((result['covered'], result['total']), (1, 2))
            Path('coverage.info').write_text('SF:a.cpp\nDA:1,-1\n')
            with self.assertRaisesRegex(ValueError, 'Invalid LCOV'):
                history.read_lcov('coverage.info', 'a')

    def test_empty_report_cannot_become_a_baseline(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'empty.info'
            path.write_text('')
            with self.assertRaisesRegex(ValueError, 'No executable lines'):
                history.read_lcov(path, 'a')

    def test_investigation_includes_recovery_and_every_intermediate_change(self):
        run = {'head_sha': 'c'}
        result = {**self.result('c'), 'baseline_sha': 'a'}
        with patch.object(history, 'latest_baseline', return_value=(0, None)), \
             patch.object(history, 'api', side_effect=[run, {'artifacts': [{'id': 9, 'name': 'coverage-baseline', 'expired': False}]}]), \
             patch.object(history, 'trusted_run', return_value=True), \
             patch.object(history, 'artifact_result', return_value=result), \
             patch.object(history, 'interval', return_value=['b', 'c']):
            plan = history.plan({'inputs': {'investigate_run': '12'}}, 'workflow_dispatch', 'd')
            self.assertEqual(plan['matrix']['sha'], ['a', 'b', 'c'])
            self.assertEqual(plan['baseline'], '0')

    def test_real_git_history_preserves_first_parent_merge_attribution(self):
        with tempfile.TemporaryDirectory() as root:
            def git(*args):
                return subprocess.check_output(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                    '-c', 'commit.gpgsign=false', *args], cwd=root, text=True).strip()
            git('init', '-q', '--initial-branch=trunk')
            git('commit', '--allow-empty', '-qm', 'base')
            base = git('rev-parse', 'HEAD')
            git('checkout', '-qb', 'feature')
            git('commit', '--allow-empty', '-qm', 'feature change')
            side = git('rev-parse', 'HEAD')
            git('checkout', '-qb', 'main', base)
            git('commit', '--allow-empty', '-qm', 'main change')
            main = git('rev-parse', 'HEAD')
            git('merge', '--no-ff', '-qm', 'Merge PR #123', 'feature')
            head = git('rev-parse', 'HEAD')
            with patch.object(history, 'git', side_effect=git):
                self.assertEqual(history.interval(base, head), [main, head])
                with self.assertRaisesRegex(ValueError, 'first-parent'):
                    history.interval(side, head)


if __name__ == '__main__':
    unittest.main()
