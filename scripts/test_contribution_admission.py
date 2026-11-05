#!/usr/bin/env python3
"""Offline policy checks. Requires PyYAML and Node.js; makes no service calls."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]


class AdmissionTests(unittest.TestCase):
    def test_review_configuration(self):
        config = yaml.safe_load((ROOT / '.coderabbit.yaml').read_text())
        self.assertFalse(config.get('chat', {}).get('allow_non_org_members', True))
        self.assertEqual(config['reviews']['auto_review']['labels'], ['maintainer-pr'])
        self.assertTrue(config['reviews']['auto_review']['enabled'])
        self.assertFalse(config['reviews']['auto_review']['drafts'])

    def test_classification(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/classify-pull-request.yml').read_text())
        steps = workflow['jobs']['classify']['steps']
        self.assertEqual(len(steps), 1)  # No PR checkout, shell, or dependency installation.
        self.assertTrue(steps[0]['uses'].startswith('actions/github-script@'))
        script = steps[0]['with']['script']
        harness = r'''
const assert = require('node:assert/strict');
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
const classify = new AsyncFunction('github', 'context', JSON.parse(process.argv[1]));
(async () => {
  for (const association of ['OWNER', 'MEMBER', 'COLLABORATOR', 'CONTRIBUTOR', 'FIRST_TIMER', 'NONE']) {
    for (const permission of ['read', 'write', 'maintain', 'admin']) {
    for (const existing of [[], ['maintainer-pr'], ['contribution']]) {
      const labels = new Set(existing);
      const calls = [];
      const github = {rest: {repos: {getCollaboratorPermissionLevel: async ({username}) => {assert.equal(username, 'author'); return {data: {permission}};}}, pulls: {get: async () => ({data: {
        state: 'open', number: 42, user: {login: 'author'}, author_association: association
      }})}, issues: {
        listLabelsOnIssue: () => {},
        getLabel: async () => {},
        removeLabel: async ({name}) => {labels.delete(name); calls.push('remove');},
        addLabels: async (args) => {args.labels.forEach(x => labels.add(x)); calls.push('add');}
      }}, paginate: async () => [...labels].map(name => ({name}))};
      await classify(github, {repo: {owner: 'k-nuth', repo: 'kth'}, issue: {number: 42}});
      const internal = ['OWNER', 'MEMBER'].includes(association) || ['admin', 'maintain', 'write'].includes(permission);
      assert.equal(labels.has('maintainer-pr'), internal);
      assert.equal(labels.has('contribution'), !internal);
      const count = calls.length;
      await classify(github, {repo: {owner: 'k-nuth', repo: 'kth'}, issue: {number: 42}});
      assert.equal(calls.length, count, 'classification must be idempotent');
    }
  }
}
})().catch(error => {console.error(error); process.exit(1);});
'''
        subprocess.run(['node', '-e', harness, json.dumps(script)], check=True)

    def test_native_ci_setting(self):
        script = ROOT / 'scripts/configure_contribution_admission.sh'
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            gh = folder / 'gh'
            gh.write_text('#!/bin/sh\n'
                          'case "$*" in\n'
                          '  *"--method PUT"*) echo "$*" >> "$CALL_LOG";;\n'
                          '  *) echo "${POLICY_RESULT:-all_external_contributors}";;\n'
                          'esac\n')
            gh.chmod(0o755)
            import os
            env = dict(os.environ, PATH=f'{folder}:{os.environ["PATH"]}', CALL_LOG=str(folder / 'calls'))
            result = subprocess.run(['bash', str(script)], env=env, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((folder / 'calls').exists())
            result = subprocess.run(['bash', str(script), '--apply'], env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('approval_policy=all_external_contributors', (folder / 'calls').read_text())
            env['POLICY_RESULT'] = 'first_time_contributors'
            result = subprocess.run(['bash', str(script), '--apply'], env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
