#!/usr/bin/env python3
"""Fail when the Conan credentials cross the boundary they are supposed to keep.

There are two vocabularies here, and mixing them is the defect this guards.

  * The repository's PHYSICAL secrets — `CONAN_LOGIN_USERNAME` and
    `CONAN_3_PASSWORD` — are facts about this GitHub repository. They belong to
    exactly one file, `main.yml`, which is where the outside world is bound to
    the inside.
  * The reusable workflows speak an INTERFACE: `conan_remote_username` and
    `conan_remote_password`, declared as `workflow_call` secrets. A reusable
    workflow that names a physical secret is coupled to the repository it
    happens to live in today, which is the opposite of what "reusable" means.

The repository reached this rule the long way. One pair of credentials had
grown three names — `conan-user`/`conan-password` in four workflows,
`CONAN_LOGIN_USERNAME`/`CONAN_PASSWORD` in two more, and the physical pair at
the top — where every hop was a correct rename in isolation and the only thing
holding the chain together was that someone had once got all six right at the
same time. Unifying the names fixed the symptom; moving the physical names into
one layer fixes the coupling.

What makes this worth a script rather than a convention: passing a name the
callee does not declare is valid YAML, says nothing to actionlint, and fails
when a job *starts* — as a message about a secret rather than about the edit
that caused it. So this reads the wiring:

  * a reusable workflow declares exactly the two interface names, and mentions
    no physical or retired name anywhere;
  * a root workflow may read the physical names and nothing else;
  * every call maps both interface names, each from its own physical source and
    not crossed — a swapped pair would try to authenticate with the password as
    the username;
  * the credentials travel as `workflow_call` secrets, never as ordinary `with:`
    inputs, which are not redacted the way secrets are;
  * and if this ever examines no caller/callee pair at all, it fails. A green
    tick from a checker that stopped looking is worse than no checker.

No secret VALUE is ever read: the inputs are workflow files on disk, which hold
`${{ secrets.NAME }}` expressions and never their contents. Output is limited to
names, paths and line numbers.

Usage: check-conan-credentials.py [--root DIR]
"""

import pathlib
import re
import sys

INTERFACE_USERNAME = "conan_remote_username"
INTERFACE_PASSWORD = "conan_remote_password"
INTERFACE = {INTERFACE_USERNAME, INTERFACE_PASSWORD}

PHYSICAL_USERNAME = "CONAN_LOGIN_USERNAME"
PHYSICAL_PASSWORD = "CONAN_3_PASSWORD"
PHYSICAL = {PHYSICAL_USERNAME, PHYSICAL_PASSWORD}

# Which physical secret each interface name is allowed to come from. Crossing
# them is valid YAML and authenticates with the password as the username.
SOURCE_OF = {INTERFACE_USERNAME: PHYSICAL_USERNAME,
             INTERFACE_PASSWORD: PHYSICAL_PASSWORD}

# Names this repository actually used before the unification, kept only so the
# error can say "retired" instead of "unexpected". The rule that does the work
# is that a name must be interface-or-physical for where it appears.
RETIRED = {
    "CONAN_PASSWORD": "the reusable build workflows",
    "conan-password": "the deps and calc-deps workflows",
    "conan-user": "the deps and calc-deps workflows",
}

# Anything that looks like it names a Conan credential — deliberately loose, to
# catch the alias nobody has invented yet.
LOOKS_LIKE_CONAN = re.compile(r"conan", re.IGNORECASE)
# Narrower: a Conan input that is a CREDENTIAL rather than a setting. Needed
# because `conan-remote` is a perfectly good ordinary input and must not be
# confused with something that has to travel as a secret.
LOOKS_LIKE_CREDENTIAL = re.compile(
    r"conan.*(password|passwd|pwd|secret|token|credential|user|login)"
    r"|(password|passwd|pwd|secret|token|credential|user|login).*conan",
    re.IGNORECASE)
SECRET_REFERENCE = re.compile(r"secrets\.([A-Za-z_][A-Za-z0-9_-]*)")
BLOCK_SCALAR = re.compile(r"^[|>][-+0-9]*$")
KEY = re.compile(r"([A-Za-z0-9_.\-\"']+)\s*:(\s|$)")


def mapping_keys(text):
    """Every mapping key in a workflow, with the path of keys above it.

    A hand-rolled reader of the YAML subset these files use, rather than a
    dependency: this has to run inside the gcc build containers, where a pip
    install is not guaranteed and a check that cannot run is a check that does
    not run. It is narrow on purpose, and the selftest feeds it the shapes that
    matter — sequences, block scalars, comments — because a parser nobody tested
    fails by finding nothing, which is the one failure this script exists to
    prevent.
    """
    entries = []
    stack = []          # [(indent, key)] of the mapping keys currently open
    skip_deeper_than = None

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip(" "))

        # Inside a `run: |` block the content is shell, not YAML, and reading it
        # as YAML is how a parser invents keys that were never there.
        if skip_deeper_than is not None:
            if indent > skip_deeper_than:
                continue
            skip_deeper_than = None

        body = line.lstrip(" ")
        if body.startswith("#"):
            continue

        if body == "-" or body.startswith("- "):
            # A sequence item. Recording it keeps a step's `uses:` from being
            # read as a job's `uses:` — they differ only by being inside this.
            while stack and stack[-1][0] >= indent:
                stack.pop()
            stack.append((indent, "[]"))
            if body == "-":
                continue
            indent += 2
            body = body[2:]

        match = KEY.match(body)
        if match is None:
            continue

        key = match.group(1).strip("\"'")
        value = body[match.end(1) + 1:].strip()

        while stack and stack[-1][0] >= indent:
            stack.pop()

        entries.append((tuple(k for _, k in stack), key, value, lineno))
        stack.append((indent, key))

        if BLOCK_SCALAR.match(value):
            skip_deeper_than = indent

    return entries


def is_reusable(entries):
    return any(path == ("on",) and key == "workflow_call" for path, key, _v, _l in entries)


def declared_secrets(entries):
    return {key: lineno for path, key, _v, lineno in entries
            if path == ("on", "workflow_call", "secrets")}


def declared_inputs(entries):
    return {key: lineno for path, key, _v, lineno in entries
            if path == ("on", "workflow_call", "inputs")}


def caller_jobs(entries):
    """Jobs that call another workflow: the target, the secrets and the inputs."""
    targets, secrets, withs = {}, {}, {}
    for path, key, value, lineno in entries:
        if len(path) == 2 and path[0] == "jobs" and key == "uses":
            targets[path[1]] = (re.sub(r"\s+#.*$", "", value).strip(), lineno)
        elif len(path) == 3 and path[0] == "jobs" and path[2] == "secrets":
            secrets.setdefault(path[1], {})[key] = (value, lineno)
        elif len(path) == 3 and path[0] == "jobs" and path[2] == "with":
            withs.setdefault(path[1], {})[key] = (value, lineno)
    return {job: (targets[job][0], targets[job][1],
                  secrets.get(job, {}), withs.get(job, {}))
            for job in targets}


def secret_references(text):
    """Every `${{ secrets.NAME }}` in the file, with its line."""
    out = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for name in SECRET_REFERENCE.findall(line):
            out.append((name, lineno))
    return out


def retired_note(name):
    if name in RETIRED:
        return (f"`{name}` was retired when the credentials were unified "
                f"(it used to be {RETIRED[name]}). ")
    return ""


def main() -> int:
    root = pathlib.Path(".")
    argv = sys.argv[1:]
    if argv[:1] == ["--root"]:
        if len(argv) < 2:
            print("::error::check-conan-credentials: --root needs a directory",
                  file=sys.stderr)
            return 1
        root = pathlib.Path(argv[1])

    workflow_dir = root / ".github" / "workflows"
    if not workflow_dir.is_dir():
        print(f"::error::check-conan-credentials: {workflow_dir} does not exist; "
              "no workflow was examined", file=sys.stderr)
        return 1

    workflows = sorted(workflow_dir.glob("*.yml")) + sorted(workflow_dir.glob("*.yaml"))
    if not workflows:
        print(f"::error::check-conan-credentials: no workflow file exists under "
              f"{workflow_dir}; nothing was examined", file=sys.stderr)
        return 1

    errors = []
    parsed, texts, reusable = {}, {}, {}

    for path in workflows:
        where = path.relative_to(root).as_posix()
        texts[where] = path.read_text()
        parsed[where] = mapping_keys(texts[where])
        reusable[where] = is_reusable(parsed[where])

    # ------------------------------------------------------------------
    # Each file may speak only its own vocabulary.
    # ------------------------------------------------------------------
    for where, entries in parsed.items():
        if reusable[where]:
            declared = {k: l for k, l in declared_secrets(entries).items()
                        if LOOKS_LIKE_CONAN.search(k)}
            references = [(n, l) for n, l in secret_references(texts[where])
                          if LOOKS_LIKE_CONAN.search(n)]

            if declared or references:
                if set(declared) != INTERFACE:
                    errors.append(
                        f"::error file={where}::check-conan-credentials: this "
                        f"reusable workflow declares "
                        f"{sorted(declared) or 'no Conan secret'} under "
                        f"workflow_call, but the interface is exactly "
                        f"{sorted(INTERFACE)}")

            for name, lineno in references:
                if name in INTERFACE:
                    continue
                extra = ("A reusable workflow must not name a physical "
                         "repository secret: that couples it to this "
                         "repository. Take it as "
                         f"`{INTERFACE_USERNAME}` / `{INTERFACE_PASSWORD}` "
                         "and let main.yml do the mapping."
                         if name in PHYSICAL else
                         f"{retired_note(name)}Use `{INTERFACE_USERNAME}` / "
                         f"`{INTERFACE_PASSWORD}`.")
                errors.append(
                    f"::error file={where},line={lineno}::"
                    f"check-conan-credentials: this reusable workflow reads "
                    f"`{name}`. {extra}")

            for name, lineno in declared_inputs(entries).items():
                if LOOKS_LIKE_CREDENTIAL.search(name):
                    errors.append(
                        f"::error file={where},line={lineno}::"
                        f"check-conan-credentials: `{name}` is declared as an "
                        "ordinary workflow_call input. A credential must be a "
                        "`secrets:` entry — inputs are not redacted in logs")
        else:
            for name, lineno in secret_references(texts[where]):
                if not LOOKS_LIKE_CONAN.search(name) or name in PHYSICAL:
                    continue
                extra = ("That is the reusable workflows' interface name, not a "
                         "repository secret; a root workflow reads the physical "
                         "one."
                         if name in INTERFACE else
                         f"{retired_note(name)}The only Conan secrets this "
                         f"repository has are {sorted(PHYSICAL)}.")
                errors.append(
                    f"::error file={where},line={lineno}::"
                    f"check-conan-credentials: `{name}` is read here. {extra}")

    # ------------------------------------------------------------------
    # Every call maps both interface names, from the right physical source.
    # ------------------------------------------------------------------
    pairs = 0
    for where, entries in parsed.items():
        for job, (target, uses_line, handed, withs) in caller_jobs(entries).items():
            if not target.startswith("./.github/workflows/"):
                continue
            callee = target[len("./"):]
            if callee not in parsed:
                errors.append(
                    f"::error file={where},line={uses_line}::"
                    f"check-conan-credentials: job `{job}` calls `{target}`, "
                    "which was not found; the credentials it needs could not be "
                    "checked")
                continue

            declared = {k for k in declared_secrets(parsed[callee])
                        if LOOKS_LIKE_CONAN.search(k)}
            passed = {k: v for k, v in handed.items() if LOOKS_LIKE_CONAN.search(k)}
            if not declared and not passed:
                continue
            pairs += 1

            for name, (value, lineno) in withs.items():
                if LOOKS_LIKE_CREDENTIAL.search(name):
                    errors.append(
                        f"::error file={where},line={lineno}::"
                        f"check-conan-credentials: job `{job}` passes `{name}` "
                        "under `with:`. A credential must go through "
                        "`secrets:`, which is redacted; `with:` is not")

            for name in sorted(INTERFACE - set(passed)):
                errors.append(
                    f"::error file={where},line={uses_line}::"
                    f"check-conan-credentials: job `{job}` calls `{target}` "
                    f"without mapping `{name}`")
            for name in sorted(set(passed) - INTERFACE):
                errors.append(
                    f"::error file={where},line={passed[name][1]}::"
                    f"check-conan-credentials: job `{job}` maps `{name}`, which "
                    f"is not part of the interface {sorted(INTERFACE)}")

            for name, (value, lineno) in sorted(passed.items()):
                if name not in SOURCE_OF:
                    continue
                sources = [n for n in SECRET_REFERENCE.findall(value)]
                if not sources:
                    errors.append(
                        f"::error file={where},line={lineno}::"
                        f"check-conan-credentials: `{name}` is mapped from "
                        "something that is not a repository secret")
                    continue
                for source in sources:
                    if source == SOURCE_OF[name]:
                        continue
                    if source in PHYSICAL:
                        errors.append(
                            f"::error file={where},line={lineno}::"
                            f"check-conan-credentials: `{name}` is mapped from "
                            f"`{source}`; the pair is crossed, and the login "
                            "would send the password as the username")
                    else:
                        errors.append(
                            f"::error file={where},line={lineno}::"
                            f"check-conan-credentials: `{name}` is mapped from "
                            f"`{source}`. {retired_note(source)}The accepted "
                            f"sources are exactly {sorted(PHYSICAL)}")

    # Concrete errors come first. "Examined nothing" is the fallback for a tree
    # that had nothing to say, not a summary that should swallow a specific
    # complaint — a caller pointing at a workflow that does not exist produces
    # both conditions, and the useful one is the missing file.
    if errors:
        for line in errors:
            print(line, file=sys.stderr)
        return 1

    if pairs == 0:
        print("::error::check-conan-credentials: no call from a workflow to a "
              "reusable workflow carrying Conan credentials was found; this "
              "check examined nothing, which it must never do quietly",
              file=sys.stderr)
        return 1

    n_reusable = sum(1 for w in reusable if reusable[w])
    print(f"check-conan-credentials: {len(workflows)} workflow(s) examined, "
          f"{n_reusable} reusable taking {INTERFACE_USERNAME} / "
          f"{INTERFACE_PASSWORD}, {pairs} call(s) mapping them from "
          f"{PHYSICAL_USERNAME} / {PHYSICAL_PASSWORD}")

    # What this check does NOT enforce, said out loud. GitHub only executes
    # workflows in the repository-root `.github/workflows`; this tree also
    # carries inert copies — the vendored `ci_utils/` workflows and the
    # per-component `knuth.yml` files left over from before the monorepo —
    # which still spell the retired names. Renaming credentials in files that
    # never run would be churn that hides the real answer, which is that they
    # should not be in the tree at all. So they are listed rather than fixed or
    # ignored: a bounded check that never says where its boundary is reads as
    # "the repository is clean" when it only ever looked at part of it.
    inert = []
    for path in sorted(root.glob("*/**/.github/workflows/*.yml")):
        try:
            text = path.read_text()
        except (UnicodeDecodeError, OSError):
            continue
        names = sorted({n for n, _l in secret_references(text)
                        if LOOKS_LIKE_CONAN.search(n)
                        and n not in INTERFACE and n not in PHYSICAL}
                       | {n for n in RETIRED
                          if re.search(rf"(?<![\w-]){re.escape(n)}(?![\w-])", text)})
        if names:
            inert.append((path.relative_to(root).as_posix(), names))

    if inert:
        print(f"check-conan-credentials: not enforced — {len(inert)} file(s) "
              "outside .github/workflows still spell a retired name. GitHub "
              "never runs them; they are dead copies and should be deleted "
              "rather than renamed:")
        for where, names in inert:
            print(f"  {where}: {', '.join(names)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
