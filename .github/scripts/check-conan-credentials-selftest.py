#!/usr/bin/env python3
"""Exercises check-conan-credentials.py against synthetic workflow trees.

The boundary under test has two sides, and both need a control. It is easy to
write a check that rejects everything — it would pass every "this must fail"
case below and be worthless — so the passing cases are as load-bearing as the
failing ones, and case 11 in particular makes sure an ordinary `conan-remote`
input is not mistaken for a credential.

The other control that matters is case 12: if this script ever stops finding
the workflows, `check-conan-credentials` must fail rather than pass, because a
green tick from a checker that examined nothing is worse than no checker.

Case 15 exists because this reads files that sit next to secrets: it puts a
literal that looks like a password into a workflow and asserts it never reaches
the output.
"""

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
CHECK = HERE / "check-conan-credentials.py"

CALLEE_OK = """\
name: reusable
on:
  workflow_call:
    inputs:
      conan-remote:
        required: true
        type: string
    secrets:
      conan_remote_username:
        required: true
      conan_remote_password:
        required: true
jobs:
  reusable-build:
    runs-on: ubuntu-24.04
    steps:
      # A step's `uses:` is not a job's `uses:`, and a parser that cannot tell
      # them apart would look for a workflow named `actions/checkout@v4`.
      - uses: actions/checkout@v4
      - name: login
        run: |
          # These lines are shell, not YAML. A reader that treats them as
          # mapping keys invents structure that was never in the file:
          #   uses: not-a-workflow.yml
          #   secrets: not-a-secrets-block
          conan remote login -p "${{ secrets.conan_remote_password }}" "${{ inputs.conan-remote }}" "${{ secrets.conan_remote_username }}"
"""

CALLER_OK = """\
name: main
on: [push]
jobs:
  build:
    uses: ./.github/workflows/reusable.yml
    with:
      conan-remote: kth
    secrets:
      conan_remote_username: ${{ secrets.CONAN_LOGIN_USERNAME }}
      conan_remote_password: ${{ secrets.CONAN_3_PASSWORD }}
"""

NO_CONAN_AT_ALL = """\
name: unrelated
on: [push]
jobs:
  lint:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: echo ${{ secrets.GITHUB_TOKEN }}
"""

FAILURES = []


def run(root):
    result = subprocess.run(
        [sys.executable, str(CHECK), "--root", str(root)],
        capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


def tree(**files):
    """A temporary repository root holding the given workflow files."""
    root = pathlib.Path(tempfile.mkdtemp())
    (root / ".github" / "workflows").mkdir(parents=True)
    for name, text in files.items():
        (root / ".github" / "workflows" / f"{name}.yml").write_text(text)
    return root


def check(label, actual, expected):
    if actual == expected:
        print(f"  ok   {label}")
    else:
        print(f"  FAIL {label}: expected [{expected}], got [{actual}]")
        FAILURES.append(label)


print("check-conan-credentials-selftest")

# 1. The shape this PR leaves behind: the reusable workflow speaks only the
#    interface, the caller maps it from the physical secrets, and the parser is
#    fooled by neither steps nor shell inside `run: |`.
code, out = run(tree(reusable=CALLEE_OK, main=CALLER_OK))
check("a consistent tree passes", code, 0)
check("it reports what it examined", "1 call(s) mapping them" in out, True)
check("it does not read a step's uses as a job's", "actions/checkout" in out, False)

# 2. THE COUPLING THIS DESIGN FORBIDS. A reusable workflow that names a physical
#    repository secret works today and is wrong: it can only ever be reused
#    somewhere that happens to spell its secrets the same way.
code, out = run(tree(
    reusable=CALLEE_OK.replace("secrets.conan_remote_password",
                               "secrets.CONAN_3_PASSWORD"),
    main=CALLER_OK))
check("a reusable workflow naming a physical secret fails", code, 1)
check("it says why that is coupling", "couples it to this" in out, True)

# 3. The interface is exactly two names, spelled exactly one way. Uppercase is
#    the near miss most likely to be written by hand.
code, out = run(tree(
    reusable=CALLEE_OK.replace("conan_remote_password", "CONAN_REMOTE_PASSWORD"),
    main=CALLER_OK.replace("conan_remote_password", "CONAN_REMOTE_PASSWORD")))
check("an almost-right interface name fails", code, 1)

# 4. The other direction: a root workflow may read the physical secrets and
#    nothing else. The interface name is not a repository secret at all, so
#    reading it there yields an empty string and a login that fails much later.
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK + """\
  extra:
    runs-on: ubuntu-24.04
    steps:
      - run: conan remote login -p "${{ secrets.conan_remote_password }}" kth
"""))
check("a root workflow reading an interface name fails", code, 1)
check("it says that is not a repository secret",
      "not a repository secret" in out, True)

# 5. A retired name coming back is named as retired, wherever it appears.
code, out = run(tree(
    reusable=CALLEE_OK.replace("secrets.conan_remote_password",
                               "secrets.conan-password"),
    main=CALLER_OK))
check("a retired name fails", code, 1)
check("the retired name is called retired", "retired" in out, True)

# 6. Half a mapping. Valid YAML, silent to actionlint, fails at job start.
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK.replace(
        "      conan_remote_password: ${{ secrets.CONAN_3_PASSWORD }}\n", "")))
check("mapping only one of the two fails", code, 1)
check("the missing one is named", "without mapping `conan_remote_password`" in out, True)

# 7. THE CROSSED PAIR. Both names present, both sources legitimate, and the
#    login would send the password as the username. Nothing but a rule about
#    which source feeds which name catches this.
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK
        .replace("conan_remote_username: ${{ secrets.CONAN_LOGIN_USERNAME }}",
                 "conan_remote_username: ${{ secrets.CONAN_3_PASSWORD }}")
        .replace("conan_remote_password: ${{ secrets.CONAN_3_PASSWORD }}",
                 "conan_remote_password: ${{ secrets.CONAN_LOGIN_USERNAME }}")))
check("a crossed mapping fails", code, 1)
check("it says the pair is crossed", "the pair is crossed" in out, True)

# 8. A source that is not one of the two physical secrets.
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK.replace("secrets.CONAN_3_PASSWORD", "secrets.CONAN_PASSWORD")))
check("an unaccepted source fails", code, 1)
check("the accepted sources are stated", "accepted sources are exactly" in out, True)

# 9. The PAT must travel as a workflow_call secret. `with:` inputs are not
#    redacted in logs the way secrets are.
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK.replace("      conan-remote: kth\n",
                           "      conan-remote: kth\n"
                           "      conan-password: ${{ secrets.CONAN_3_PASSWORD }}\n")))
check("a credential passed under with: fails", code, 1)
check("it says with: is not redacted", "which is redacted" in out, True)

# 10. Same rule read from the callee's side.
code, out = run(tree(
    reusable=CALLEE_OK.replace("      conan-remote:\n        required: true\n        type: string\n",
                               "      conan-remote:\n        required: true\n        type: string\n"
                               "      conan-user:\n        required: true\n        type: string\n"),
    main=CALLER_OK))
check("a credential declared as an input fails", code, 1)
check("it says inputs are not redacted", "not redacted in logs" in out, True)

# 11. THE OVER-MATCHING CONTROL. `conan-remote` is a setting, not a credential.
#     A check that treats every conan-ish input as a secret would fail case 1
#     and every real build with it.
code, out = run(tree(reusable=CALLEE_OK, main=CALLER_OK))
check("an ordinary conan-remote input is not a credential", code, 0)

# 12. THE EMPTY CONTROL. Nothing to examine must never be silence.
code, out = run(tree())
check("an empty workflow directory fails", code, 1)
check("it says nothing was examined", "nothing was examined" in out, True)

code, out = run(pathlib.Path(tempfile.mkdtemp()))
check("a missing workflow directory fails", code, 1)

code, out = run(tree(unrelated=NO_CONAN_AT_ALL))
check("a tree with no call carrying credentials fails", code, 1)
check("it says it examined nothing", "examined nothing" in out, True)

code, out = run(tree(reusable=CALLEE_OK))
check("a callee with no caller at all fails", code, 1)

# 13. Not a blanket refusal: the passing case still passes beside unrelated
#     workflows, and with more than one call.
code, out = run(tree(
    reusable=CALLEE_OK, main=CALLER_OK, unrelated=NO_CONAN_AT_ALL))
check("unrelated workflows do not break the check", code, 0)

code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK + CALLER_OK.split("jobs:\n", 1)[1].replace("  build:", "  build2:")))
check("two calls both pass and are counted", code, 0)
check("both calls are counted", "2 call(s) mapping them" in out, True)

# 14. A caller pointing at a workflow that is not there is "could not check",
#     never "checked and fine".
code, out = run(tree(
    reusable=CALLEE_OK,
    main=CALLER_OK.replace("./.github/workflows/reusable.yml",
                           "./.github/workflows/absent.yml")))
check("a missing callee fails", code, 1)
check("it says it could not be checked", "could not be checked" in out, True)

# 15. No value ever reaches the output. The literal below is not a credential,
#     it is bait: if the reporting ever echoes what it read instead of what it
#     was called, this catches it.
BAIT = "s3cr3t-bait-value-not-a-real-password"
code, out = run(tree(
    reusable=CALLEE_OK.replace("secrets.conan_remote_password",
                               "secrets.CONAN_PASSWORD"),
    main=CALLER_OK.replace("${{ secrets.CONAN_3_PASSWORD }}", BAIT)))
check("the bait case does fail", code, 1)
check("no value is printed", BAIT in out, False)

# 16. Inert copies elsewhere in the tree are reported, not enforced — and the
#     report has to be complete. The two shapes differ: a vendored workflow
#     READS `${{ secrets.conan-password }}`, while the per-component leftovers
#     only HAND OVER a value under a retired key. A scan that looked for reads
#     alone would list the first and quietly miss the second.
root = tree(reusable=CALLEE_OK, main=CALLER_OK)
for where, text in (
    ("vendored", "run: conan remote login -p ${{ secrets.conan-password }} kth\n"),
    ("src/component", "secrets:\n  conan-user: ${{ secrets.CONAN_LOGIN_USERNAME }}\n"),
):
    d = root / where / ".github" / "workflows"
    d.mkdir(parents=True)
    (d / "old.yml").write_text(text)
code, out = run(root)
check("inert copies do not fail the check", code, 0)
check("a read of a retired name is listed",
      "vendored/.github/workflows/old.yml" in out, True)
check("a retired name used only as a key is listed too",
      "src/component/.github/workflows/old.yml" in out, True)
check("the report says it is not enforced", "not enforced" in out, True)

if FAILURES:
    print(f"check-conan-credentials-selftest: {len(FAILURES)} failure(s): "
          + ", ".join(FAILURES))
    sys.exit(1)
print("check-conan-credentials-selftest: all checks passed")
