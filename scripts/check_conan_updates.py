#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Check for updates to Conan dependencies from Conan Center Index.

Usage:
    check_conan_updates.py [--update] [--package PACKAGE_NAME]

Options:
    --update              Update conanfile.py with latest versions (default: read-only)
    --package PACKAGE     Check/update only a specific package
    -h, --help           Show this help message
"""

import argparse
import json
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import yaml


CUSTOM_PACKAGE_REMOTES = {
    "secp256k1-precompute": "kth",
    "utxoz": "kth",
}


class PackageLookupError(RuntimeError):
    """A package source could not provide a complete version listing."""


class Version:
    """Semantic version parser and comparator."""

    def __init__(self, version_str: str):
        self.original = version_str
        # Parse version string (e.g., "1.89.0" or "11.2.0" or "0.9.32")
        parts = version_str.split('.')
        try:
            self.parts = [int(p) for p in parts]
        except ValueError:
            # If we can't parse as integers, it's not a valid semantic version
            # Try to parse leading digits only
            self.parts = []
            for p in parts:
                # Extract leading digits
                digits = ''
                for c in p:
                    if c.isdigit():
                        digits += c
                    else:
                        break
                if digits:
                    self.parts.append(int(digits))
                else:
                    # Can't parse this part, skip
                    break

    @staticmethod
    def is_valid(version_str: str) -> bool:
        """Check if a version string is valid (all numeric parts)."""
        parts = version_str.split('.')
        for p in parts:
            if not p.isdigit():
                return False
        return len(parts) > 0

    def __lt__(self, other):
        # Compare versions part by part
        for i in range(max(len(self.parts), len(other.parts))):
            a = self.parts[i] if i < len(self.parts) else 0
            b = other.parts[i] if i < len(other.parts) else 0
            if a < b:
                return True
            elif a > b:
                return False
        return False

    def __eq__(self, other):
        return self.parts == other.parts

    def __str__(self):
        return self.original

    def __repr__(self):
        return f"Version({self.original})"


def parse_conanfile(conanfile_path: Path) -> Dict[str, str]:
    """
    Parse conanfile.py and extract package versions from requires() and tool_requires().

    Returns:
        Dict mapping package name to version string
    """
    packages = {}

    with open(conanfile_path, 'r') as f:
        lines = f.readlines()

    # Match self.requires("package/version", ...) and self.test_requires("package/version")
    # Only process non-commented lines
    requires_pattern = r'self\.(?:requires|test_requires|tool_requires)\("([^/]+)/([^"]+)"'

    for line in lines:
        # Skip commented lines
        stripped = line.strip()
        if stripped.startswith('#'):
            continue

        for match in re.finditer(requires_pattern, line):
            package_name = match.group(1)
            version = match.group(2)

            # Skip versions with @ (e.g., "1.6.34@kth/stable")
            if '@' in version:
                continue

            packages[package_name] = version

    return packages


def fetch_cci_versions(package_name: str) -> List[str]:
    """
    Fetch available versions from Conan Center Index for a package.

    Args:
        package_name: Name of the package (e.g., "boost", "fmt")

    Returns:
        List of version strings.

    Raises:
        PackageLookupError: The package could not be looked up completely.
    """
    url = f"https://raw.githubusercontent.com/conan-io/conan-center-index/master/recipes/{package_name}/config.yml"

    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            content = response.read().decode('utf-8')
            data = yaml.safe_load(content)

            if data.get('versions'):
                return list(data['versions'].keys())

    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise PackageLookupError(
                "package not found in CCI; if it is hosted by KTH, add its "
                "Conan remote to CUSTOM_PACKAGE_REMOTES"
            ) from e
        else:
            raise PackageLookupError(f"CCI returned HTTP {e.code}") from e
    except Exception as e:
        raise PackageLookupError(f"CCI lookup failed: {e}") from e

    raise PackageLookupError("CCI response contained no versions")


def fetch_conan_remote_versions(package_name: str, remote: str) -> List[str]:
    """Fetch recipe versions from a configured Conan remote."""
    command = [
        "conan", "list", f"{package_name}/*", "-r", remote, "--format=json"
    ]
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError) as e:
        raise PackageLookupError(f"Conan remote '{remote}' lookup failed: {e}") from e

    if completed.returncode != 0:
        detail = completed.stderr.strip() or f"exit status {completed.returncode}"
        raise PackageLookupError(f"Conan remote '{remote}' lookup failed: {detail}")

    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as e:
        raise PackageLookupError(
            f"Conan remote '{remote}' returned invalid JSON"
        ) from e

    versions = []
    for recipes in payload.values():
        if not isinstance(recipes, dict):
            continue
        for reference in recipes:
            prefix = f"{package_name}/"
            if not reference.startswith(prefix):
                continue
            version = reference[len(prefix):].split("@", 1)[0].split("#", 1)[0]
            if version:
                versions.append(version)

    if not versions:
        raise PackageLookupError(
            f"package not found in Conan remote '{remote}'"
        )

    return versions


def get_latest_version(versions: List[str]) -> Version:
    """
    Get the latest version from a list of version strings.

    Args:
        versions: List of version strings

    Returns:
        Latest Version object
    """
    # Filter only valid semantic versions
    valid_versions = [v for v in versions if Version.is_valid(v)]
    if not valid_versions:
        # Fall back to all versions if none are "valid"
        valid_versions = versions

    version_objects = [Version(v) for v in valid_versions]
    return max(version_objects)


def update_conanfile(conanfile_path: Path, updates: List[Tuple[str, str, str]]) -> None:
    """
    Update conanfile.py with new package versions.

    Args:
        conanfile_path: Path to conanfile.py
        updates: List of (package_name, old_version, new_version) tuples
    """
    with open(conanfile_path, 'r') as f:
        content = f.read()

    original_content = content

    for package_name, old_version, new_version in updates:
        # Match self.requires("package/old_version", ...)
        # Use word boundaries and be careful with special regex characters
        pattern = rf'(self\.(?:requires|test_requires|tool_requires)\("{re.escape(package_name)}/){re.escape(old_version)}(")'
        replacement = rf'\g<1>{new_version}\g<2>'
        content = re.sub(pattern, replacement, content)

    if content != original_content:
        with open(conanfile_path, 'w') as f:
            f.write(content)
        print(f"\n✅ Updated {conanfile_path.name}")
    else:
        print(f"\n⚠️  No changes made to {conanfile_path.name}")


def check_updates(
    conanfile_path: Path,
    package_filter: Optional[str] = None,
) -> Tuple[List[Tuple[str, str, str]], List[Tuple[str, str]]]:
    """
    Check for updates to packages in conanfile.py.

    Args:
        conanfile_path: Path to conanfile.py
        package_filter: If specified, only check this package

    Returns:
        A pair containing updates and lookup failures.
    """
    print(f"📦 Checking dependencies in {conanfile_path.name}...")
    if package_filter:
        print(f"   Filtering for package: {package_filter}")
    print()

    packages = parse_conanfile(conanfile_path)

    # Apply package filter if specified
    if package_filter:
        if package_filter not in packages:
            print(f"❌ Package '{package_filter}' not found in conanfile.py")
            print(f"   Available packages: {', '.join(sorted(packages.keys()))}")
            return [], [(package_filter, "package not found in conanfile.py")]
        packages = {package_filter: packages[package_filter]}

    updates = []
    failures = []

    for package_name, current_version in sorted(packages.items()):
        print(f"Checking {package_name}...", end=' ')
        sys.stdout.flush()

        remote = CUSTOM_PACKAGE_REMOTES.get(package_name)
        source = f"{remote} remote" if remote else "CCI"
        try:
            versions = (
                fetch_conan_remote_versions(package_name, remote)
                if remote
                else fetch_cci_versions(package_name)
            )
        except PackageLookupError as e:
            print(f"❌ Lookup failed [{source}]: {e}")
            failures.append((package_name, str(e)))
            continue

        latest_version = get_latest_version(versions)
        current = Version(current_version)
        suffix = f" [{source}]"

        if current < latest_version:
            print(f"🔄 Update available: {current_version} → {latest_version}{suffix}")
            updates.append((package_name, current_version, str(latest_version)))
        elif current == latest_version:
            print(f"✅ Up to date ({current_version}){suffix}")
        else:
            print(f"⚡ Ahead of {source} ({current_version} > {latest_version})")

    return updates, failures


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Check for updates to Conan dependencies from Conan Center Index",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Check all packages (read-only)
  %(prog)s

  # Check a specific package
  %(prog)s --package boost

  # Update all packages to latest versions
  %(prog)s --update

  # Update a specific package
  %(prog)s --update --package fmt
        """
    )
    parser.add_argument(
        '--update',
        action='store_true',
        help='Update conanfile.py with latest versions (default: read-only)'
    )
    parser.add_argument(
        '--package',
        type=str,
        metavar='PACKAGE',
        help='Check/update only a specific package'
    )

    args = parser.parse_args()

    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    conanfile = repo_root / "conanfile.py"

    if not conanfile.exists():
        print(f"❌ Error: {conanfile} not found", file=sys.stderr)
        sys.exit(2)

    try:
        import yaml
    except ImportError:
        print("❌ Error: PyYAML is required. Install it with: pip install pyyaml", file=sys.stderr)
        sys.exit(2)

    updates, failures = check_updates(conanfile, package_filter=args.package)

    print()
    print("=" * 70)

    if updates:
        print(f"📊 Summary: {len(updates)} package(s) have updates available:")
        print()
        for package_name, current, latest in updates:
            print(f"  • {package_name}: {current} → {latest}")

        if args.update and not failures:
            print()
            print("=" * 70)
            print("🔧 Updating conanfile.py...")
            update_conanfile(conanfile, updates)
    elif not failures:
        print("✨ All packages are up to date!")

    if failures:
        print()
        print(f"❌ Incomplete check: {len(failures)} package lookup(s) failed:")
        print()
        for package_name, reason in failures:
            print(f"  • {package_name}: {reason}")

    print("=" * 70)

    # A partial result must never be reported as success or used to create an
    # update issue: it cannot establish which dependencies are current.
    if failures:
        sys.exit(2)
    elif updates and not args.update:
        sys.exit(1)
    else:
        sys.exit(0)


def entrypoint():
    """Run the checker while preserving the documented exit-code contract."""
    try:
        main()
    except SystemExit:
        # argparse and main() use SystemExit for the documented 0/1/2 outcomes.
        raise
    except Exception as e:
        # Exit 1 has exactly one meaning: a complete check found updates.
        # Anything the checker did not classify is an incomplete check instead,
        # otherwise the scheduled workflow would publish a misleading update
        # issue for a broken run.
        print(f"❌ Unexpected dependency-check failure: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    entrypoint()
