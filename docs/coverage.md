# Coverage measurements

Normal build, test and sanitizer workflows continue to run on pull requests.
The instrumented Coverage workflow runs at 03:00 in `Europe/Madrid`, including
summer time. GitHub schedules can start late when runners are busy.

A scheduled run measures master only when its commit differs from the last
retained successful measurement. A failed night does not consume that commit.
A failed test intentionally prevents report generation, comparison and baseline
publication, including an intermittent failure: a partial test run is not a
comparable measurement. The next night retries the unmeasured commit.
Without a retained baseline, the next measurement establishes one. Results are
retained for 90 days; no historical baseline is inferred from a green check.

## Request coverage

- On a PR, add the `coverage` label. It requests coverage for the current PR
  merge revision, including fork PRs. While the label remains, subsequent pushes
  also request coverage. Adding other labels does not request another measurement.
  Remove and re-add `coverage` to repeat the request unchanged.
  Repository approval rules for external contributors still apply.
- From Actions → Coverage → Run workflow, select a branch and leave
  `investigate_run` empty. From a terminal:

  ```sh
  gh workflow run coverage.yml --repo k-nuth/kth --ref master
  ```

PR code runs under `pull_request`, with read-only repository permissions and no
fork secrets. It is never executed under `pull_request_target`. Only master
measurements from this workflow's scheduled or manual runs can become a baseline.

## Read a regression

The run summary shows covered/total lines, the percentage change, files entering
or leaving the measured scope, file-level decreases and previously covered
unchanged source lines that became uncovered. Source lines are matched across
insertions and deletions; a shifted line number alone is not a loss. Changed
source is reflected in file and total percentages, without claiming that a
rewritten line is the same old executable line.

Links to first-parent commits between the measurements identify candidate
changes. Squash and merge subjects normally identify the PR. This list is not
proof that the last commit caused the change.

To investigate a master run, enter its numeric Actions run ID in
`investigate_run`, selecting the master branch:

```sh
gh workflow run coverage.yml --repo k-nuth/kth --ref master \
  -f investigate_run=123456789
```

The workflow remeasures its original baseline and every first-parent commit
through the target, with at most one coverage build running at a time. The final
summary compares each adjacent pair, so a later recovery cannot hide an earlier
drop. A merge is attributed to the merge commit; investigating its internal
branch history is separate. Failed builds produce an inconclusive transition,
not an accusation. Repeated measurements can also expose nondeterministic tests
or toolchain differences; a measured drop needs review before assigning cause.

Investigation is limited to 20 changes per request and requires the selected
run's retained baseline metadata. It does not advance the nightly baseline.
Artifacts include `result.json` and `coverage.info` for further analysis.
