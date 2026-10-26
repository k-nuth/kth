#!/usr/bin/env python3
"""Temporarily seed LMDB's verified source archive for CI builds.

Remove this bootstrap after CCI #31261 is published and the locks use that
recipe revision. The pinned LMDB recipe itself remains unchanged.
"""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


LMDB_URLS = (
    "https://github.com/openldap/openldap/archive/refs/tags/LMDB_1.0.2.tar.gz",
    "https://gitlab.com/openldap/openldap/-/archive/LMDB_1.0.2/openldap-LMDB_1.0.2.tar.gz",
    # Conan 2.31 also checks URL membership in the cache metadata. Record the
    # pinned recipe's URL even when an earlier mirror supplies the archive.
    "https://git.openldap.org/openldap/openldap/-/archive/LMDB_1.0.2/openldap-LMDB_1.0.2.tar.gz",
)
LMDB_SHA256 = "38a029bb3e29820380e18a2dfdbe906bbc5b322132c63274c5e4ed7833821752"


def prepare(cache_dir, *, urls=LMDB_URLS, sha256=LMDB_SHA256, env=None):
    cache_dir = Path(cache_dir).expanduser().resolve()
    cache_dir.mkdir(parents=True, exist_ok=True)
    # Let Conan manage its cache layout and metadata. Verify warm cache hits too.
    recipe = f"""from conan import ConanFile
from conan.tools.files import download, check_sha256

class SourceBackup(ConanFile):
    def source(self):
        archive = "lmdb-source.tar.gz"
        download(self, url={list(urls)!r}, filename=archive,
                 sha256={sha256!r}, retry=1, retry_wait=1)
        check_sha256(self, archive, {sha256!r})
"""
    with tempfile.TemporaryDirectory(prefix="kth-source-backup-") as directory:
        Path(directory, "conanfile.py").write_text(recipe, encoding="utf-8")
        result = subprocess.run(
            ["conan", "source", directory, "-cc",
             f"core.sources:download_cache={cache_dir}"],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=180, check=False,
        )
        result.check_returncode()
        return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(prepare(args.cache_dir), end="")
    except subprocess.CalledProcessError as error:
        print(error.output, end="", file=sys.stderr)
        return error.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
