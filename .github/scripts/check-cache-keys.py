#!/usr/bin/env python3
"""Assert that every ccache restore keeps both `key` and `restore-keys`.

This exists because of a defect that shipped in the first revision of #629: an
edit inserted a step between a restore's `key` and its `restore-keys`, and the
`restore-keys` line landed inside the new step's `run:` block. The YAML stayed
valid, actionlint had nothing to say, and the only symptom would have been every
container build compiling cold — the exact failure that PR was written to fix,
introduced by the fix.

A primary key that ends in the run id can never match. `restore-keys` is the
only thing that makes a restore possible at all, so losing it is not a
degradation, it is a total loss, and it is invisible to every other check.
"""

import sys
import pathlib
import yaml

WORKFLOWS = pathlib.Path(".github/workflows")


def restore_steps(doc):
    for job in (doc.get("jobs") or {}).values():
        for step in job.get("steps") or []:
            uses = step.get("uses") or ""
            if "actions/cache" in uses and "/save" not in uses:
                yield step


def main() -> int:
    failures = []
    checked = 0

    for path in sorted(WORKFLOWS.glob("*.yml")):
        try:
            doc = yaml.safe_load(path.read_text())
        except yaml.YAMLError as exc:
            # Unreadable is not "no findings". Say which file and stop.
            failures.append(f"{path}: could not be parsed ({exc.__class__.__name__})")
            continue
        if not isinstance(doc, dict):
            continue

        for step in restore_steps(doc):
            name = step.get("name", "<unnamed>")
            with_ = step.get("with") or {}
            key = str(with_.get("key", ""))

            # Only the caches whose key rotates per run depend on restore-keys.
            # A key that is stable across runs matches on its own.
            if "github.run_id" not in key and "github.sha" not in key:
                continue

            checked += 1
            if "restore-keys" not in with_:
                failures.append(
                    f"{path}: step {name!r} has a per-run primary key and no "
                    f"restore-keys, so it can never restore anything"
                )

    if checked == 0:
        # Nothing examined is its own answer: either the workflows changed shape
        # or this check is looking in the wrong place. Either way it is not a
        # pass, because a check that silently examines nothing always passes.
        print("::error::check-cache-keys: no per-run cache restore was examined", file=sys.stderr)
        return 1

    for f in failures:
        print(f"::error::check-cache-keys: {f}", file=sys.stderr)

    if failures:
        return 1

    print(f"check-cache-keys: {checked} per-run cache restore(s), all keep restore-keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
