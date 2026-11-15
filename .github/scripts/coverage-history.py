#!/usr/bin/env python3
"""Plan measurements and compare retained, commit-addressed LCOV results."""
import argparse
import difflib
import io
import json
import os
from pathlib import Path
import subprocess
import urllib.request
import urllib.error
import zipfile


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def api(path, binary=False):
    request = urllib.request.Request('https://api.github.com/repos/' + os.environ['GITHUB_REPOSITORY'] + '/' + path,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'], 'Accept': 'application/vnd.github+json'})
    # urllib strips Authorization on redirects only across certain handlers;
    # artifact downloads use a separate, unauthenticated request to blob storage.
    if binary:
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *args):
                return None
        try:
            return urllib.request.build_opener(NoRedirect).open(request, timeout=60).read()
        except urllib.error.HTTPError as error:
            if error.code != 302:
                raise
            return urllib.request.urlopen(error.headers['Location'], timeout=60).read()
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def artifact_result(artifact_id):
    data = api(f'actions/artifacts/{int(artifact_id)}/zip', binary=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        return json.loads(archive.read('result.json'))


def trusted_run(run):
    return (run['path'] == '.github/workflows/coverage.yml'
            and run['event'] in ('schedule', 'workflow_dispatch')
            and run['head_branch'] == 'master'
            and run['head_repository']['full_name'] == os.environ['GITHUB_REPOSITORY'])


def latest_baseline():
    # Do not trust PR artifacts, even when they use the same artifact name.
    for page in range(1, 11):
        artifacts = api(f'actions/artifacts?name=coverage-baseline&per_page=100&page={page}')['artifacts']
        for artifact in artifacts:
            if artifact['expired']:
                continue
            run = api(f"actions/runs/{artifact['workflow_run']['id']}")
            if trusted_run(run):
                result = artifact_result(artifact['id'])
                if result['sha'] == run['head_sha']:
                    return artifact['id'], result
        if len(artifacts) < 100:
            break
    return 0, None


def interval(base, head):
    if git('merge-base', base, head) != base:
        raise ValueError('Baseline is not an ancestor of the target')
    commits = git('rev-list', '--first-parent', '--reverse', f'{base}..{head}').splitlines()
    if base not in git('rev-list', '--first-parent', head).splitlines():
        raise ValueError('Baseline is not on the target first-parent history')
    return commits


def plan(event, event_name, sha):
    inputs = event.get('inputs') or {}
    investigating = bool(inputs.get('investigate_run'))
    baseline_id, baseline = latest_baseline()
    if investigating:
        run_id = int(inputs['investigate_run'])
        run = api(f'actions/runs/{run_id}')
        if not trusted_run(run):
            raise ValueError('Investigation requires a master coverage run')
        artifacts = api(f'actions/runs/{run_id}/artifacts')['artifacts']
        selected = next(a for a in artifacts if a['name'] == 'coverage-baseline' and not a['expired'])
        result = artifact_result(selected['id'])
        base, head = result['baseline_sha'], result['sha']
        if not base or head != run['head_sha']:
            raise ValueError('Selected run has no comparable baseline')
        commits = [base] + interval(base, head)
        if len(commits) > 21:
            raise ValueError('Investigation exceeds 20 changes; measure a smaller interval first')
        return {'matrix': {'sha': commits}, 'baseline': '0', 'investigate': 'true', 'run': 'true'}
    should_run = event_name != 'schedule' or baseline is None or baseline['sha'] != sha
    return {'matrix': {'sha': [sha]}, 'baseline': str(baseline_id),
            'investigate': 'false', 'run': str(should_run).lower()}


def read_lcov(filename, sha):
    files, current = {}, None
    for line in Path(filename).read_text().splitlines():
        if line.startswith('SF:'):
            name = line[3:]
            path = Path(name)
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Coverage paths must be repository relative')
            current = files.setdefault(name, {'hits': {}, 'source': path.read_text().splitlines()})
        elif line.startswith('DA:'):
            number, hits, *_ = line[3:].split(',')
            if current is None or int(number) < 1 or int(hits) < 0:
                raise ValueError('Invalid LCOV line record')
            key = str(int(number))
            current['hits'][key] = current['hits'].get(key, 0) + int(hits)
    total = sum(len(f['hits']) for f in files.values())
    if not total:
        raise ValueError('No executable lines in coverage report')
    covered = sum(sum(v > 0 for v in f['hits'].values()) for f in files.values())
    return {'sha': sha, 'covered': covered, 'total': total, 'files': files}


def percentage(result):
    return 100 * result['covered'] / result['total']


def compare(before, after):
    rows = []
    for name in sorted(set(before['files']) | set(after['files'])):
        old, new = before['files'].get(name), after['files'].get(name)
        if old is None or new is None:
            rows.append(f'- `{name}`: ' + ('added to' if old is None else 'removed from') + ' measured scope')
            continue
        lost = []
        # Map unchanged source lines, rather than comparing shifted line numbers.
        for a, b, size in difflib.SequenceMatcher(None, old['source'], new['source'], autojunk=False).get_matching_blocks():
            for offset in range(size):
                if old['hits'].get(str(a + offset + 1), 0) > 0 and new['hits'].get(str(b + offset + 1)) == 0:
                    lost.append(b + offset + 1)
        old_hits, new_hits = old['hits'], new['hits']
        old_rate = sum(v > 0 for v in old_hits.values()) / len(old_hits) if old_hits else 0
        new_rate = sum(v > 0 for v in new_hits.values()) / len(new_hits) if new_hits else 0
        if lost or new_rate < old_rate:
            rows.append(f'- `{name}`: {100 * old_rate:.2f}% → {100 * new_rate:.2f}%; '
                        f'previously covered unchanged lines now uncovered: {lost}')
    delta = percentage(after) - percentage(before)
    title = f"{before['sha'][:12]} → {after['sha'][:12]}: {percentage(before):.3f}% → {percentage(after):.3f}% ({delta:+.3f} pp)"
    return title, rows


def report(result, baseline, repository):
    lines = [f"## Coverage at `{result['sha']}`", f"{result['covered']}/{result['total']} lines: {percentage(result):.3f}%", '']
    result['baseline_sha'] = baseline['sha'] if baseline else None
    if baseline:
        title, changes = compare(baseline, result)
        lines += [title, '', *changes, '', 'Changes between measurements (candidates, not proven causes):', '']
        commits = interval(baseline['sha'], result['sha'])
        for sha in commits:
            # Squash titles normally include the PR number; merge commits retain it too.
            subject = git('show', '-s', '--format=%s', sha).replace('<', '&lt;').replace('>', '&gt;')
            lines.append(f'- [{sha[:12]}](https://github.com/{repository}/commit/{sha}) {subject}')
        lines += ['', 'To investigate a master regression, run Coverage manually with investigate_run set to this run ID.',
                  'Each first-parent change is measured, including drops later recovered. Build failures remain inconclusive.']
    else:
        lines.append('No retained baseline; this measurement establishes a reference.')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['plan', 'measure', 'investigate'])
    parser.add_argument('--baseline', default='0')
    args = parser.parse_args()
    if args.command == 'plan':
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
        output = plan(event, os.environ['GITHUB_EVENT_NAME'], os.environ['GITHUB_SHA'])
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            for key, value in output.items():
                stream.write(f'{key}={json.dumps(value) if isinstance(value, dict) else value}\n')
    elif args.command == 'measure':
        sha = git('rev-parse', 'HEAD')
        result = read_lcov('coverage.info', sha)
        baseline = artifact_result(args.baseline) if args.baseline != '0' else None
        # PR merge refs can diverge from a newer nightly result. Compare only an ancestor.
        if baseline and baseline['sha'] not in git('rev-list', '--first-parent', sha).splitlines():
            baseline = None
        summary = report(result, baseline, os.environ['GITHUB_REPOSITORY'])
        Path('result.json').write_text(json.dumps(result))
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write(summary)
    else:
        expected = json.loads(os.environ['EXPECTED_COMMITS'])
        results = [json.loads(path.read_text()) for path in Path('results').rglob('result.json')]
        by_sha = {r['sha']: r for r in results}
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write('## Coverage investigation\nMeasured transitions identify changes, not authors at fault.\n\n')
            for base, head in zip(expected, expected[1:]):
                if base not in by_sha or head not in by_sha:
                    stream.write(f'- {base} → {head}: **inconclusive: missing successful measurement**\n')
                    continue
                title, changes = compare(by_sha[base], by_sha[head])
                stream.write(f'### [{title}](https://github.com/{os.environ["GITHUB_REPOSITORY"]}/commit/{head})\n\n')
                stream.write('\n'.join(changes) + '\n\n')
        if set(expected) - set(by_sha):
            raise ValueError('Investigation incomplete; see summary')


if __name__ == '__main__':
    main()
