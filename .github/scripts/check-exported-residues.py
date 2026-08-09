#!/usr/bin/env python3
"""Fail when a file the recipe exports is an editor residue.

`third_party/parlayhash/include/parlay/internal/thread_ids.h~` was tracked and
therefore copied into every published package — the export announced it on every
run (`Copied 1 '.h~' file`) and nobody read the line. A package is a claim about
what was built; a file nobody includes, nobody reviewed and nobody can explain
should not be inside one, and since #632 its bytes are part of a recipe revision
that is now a guarantee.

Scope is deliberate:

  * TRACKED files only, taken from git. The build tree is full of legitimate
    intermediates and scanning it would produce noise that trains people to
    ignore this check;
  * only under the roots the RECIPE exports, read out of `conanfile.py` rather
    than repeated here. A copy of that list would drift, and the check would
    then guard a set of directories the recipe no longer publishes.
"""

import pathlib
import re
import subprocess
import sys

RECIPE = pathlib.Path("conanfile.py")

# Emacs backups and lock files, vim swap files. Deliberately narrow: this fails
# a build, so it names shapes that are unambiguously nobody's source.
RESIDUE = re.compile(r"(~$)|(^\.#)|(\.swp$)|(\.swo$)")


def export_roots() -> list[str]:
    """The directories the recipe exports, from the recipe itself."""
    text = RECIPE.read_text()
    match = re.search(r"^\s*exports_sources\s*=\s*(.+)$", text, re.MULTILINE)
    if match is None:
        # Not "no roots, nothing to check". The recipe changed shape and this
        # check no longer knows what is published, which is a reason to stop
        # rather than to pass.
        sys.exit("::error::check-exported-residues: could not find exports_sources "
                 "in conanfile.py; this check no longer knows what is exported")

    roots = re.findall(r"[\"']([^\"']+)[\"']", match.group(1))
    if not roots:
        sys.exit("::error::check-exported-residues: exports_sources was found but "
                 "no entries could be read from it")
    return roots


def tracked_files() -> list[str]:
    result = subprocess.run(["git", "ls-files", "-z"],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit("::error::check-exported-residues: git ls-files failed; "
                 "no file was examined")
    return [p for p in result.stdout.split("\0") if p]


def under_export_root(path: str, roots: list[str]) -> bool:
    for root in roots:
        prefix = root[:-1] if root.endswith("*") else root
        if path == prefix or path.startswith(prefix.rstrip("/") + "/"):
            return True
        if path == root:
            return True
    return False


def main() -> int:
    roots = export_roots()
    files = tracked_files()

    examined = [p for p in files if under_export_root(p, roots)]
    if not examined:
        # A check that examined nothing always passes, which is the one result
        # it must never give quietly.
        print("::error::check-exported-residues: no tracked file matched the "
              f"recipe's export roots ({', '.join(roots)}); nothing was examined",
              file=sys.stderr)
        return 1

    found = sorted(p for p in examined if RESIDUE.search(pathlib.Path(p).name))

    if found:
        for path in found:
            print(f"::error file={path}::check-exported-residues: {path} is an "
                  "editor residue and is exported by the recipe, so it ships "
                  "inside every package", file=sys.stderr)
        return 1

    print(f"check-exported-residues: {len(examined)} exported file(s) under "
          f"{len(roots)} root(s), no editor residues")
    return 0


if __name__ == "__main__":
    sys.exit(main())
