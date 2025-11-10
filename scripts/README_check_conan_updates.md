# Conan Dependency Update Checker

This script checks for updates to Conan dependencies by querying the [Conan Center Index](https://github.com/conan-io/conan-center-index).

## Requirements

- Python 3.7+
- PyYAML: `pip install pyyaml`

## Usage

### Check all packages (read-only)

```bash
python scripts/check_conan_updates.py
```

This will check all dependencies in `conanfile.py` and report which ones have updates available.

### Check a specific package

```bash
python scripts/check_conan_updates.py --package boost
```

### Update all packages

```bash
python scripts/check_conan_updates.py --update
```

This will automatically update `conanfile.py` with the latest versions from Conan Center Index.

### Update a specific package

```bash
python scripts/check_conan_updates.py --update --package fmt
```

## Output

The script provides colored output with the following indicators:

- ✅ **Up to date**: Package is already using the latest version
- 🔄 **Update available**: A newer version is available in CCI
- ⚡ **Ahead of CCI**: Your version is newer than CCI (custom/pre-release)
- ❌ **Not found in CCI**: Package not found in Conan Center Index

## Exit Codes

- `0`: All packages are up to date, or updates were successfully applied
- `1`: Updates are available (useful for CI/CD to detect outdated dependencies)

## Automation

The repository includes a GitHub Actions workflow (`.github/workflows/check-conan-updates.yml`) that:

- Runs every day at 9:00 AM UTC
- Can be manually triggered via GitHub Actions UI
- Creates/updates a GitHub issue when updates are available
- Uploads a detailed report as an artifact

## Examples

```bash
# Check all packages
$ python scripts/check_conan_updates.py
📦 Checking dependencies in conanfile.py...

Checking boost... 🔄 Update available: 1.86.0 → 1.89.0
Checking fmt... ✅ Up to date (11.2.0)
Checking gmp... ✅ Up to date (6.3.0)

======================================================================
📊 Summary: 1 package(s) have updates available:

  • boost: 1.86.0 → 1.89.0
======================================================================

# Update boost only
$ python scripts/check_conan_updates.py --update --package boost
📦 Checking dependencies in conanfile.py...
   Filtering for package: boost

Checking boost... 🔄 Update available: 1.86.0 → 1.89.0

======================================================================
📊 Summary: 1 package(s) have updates available:

  • boost: 1.86.0 → 1.89.0

======================================================================
🔧 Updating conanfile.py...

✅ Updated conanfile.py
======================================================================
```

## Notes

- The script only checks packages listed in `self.requires()`, `self.test_requires()`, and `self.tool_requires()`
- Commented-out dependencies are ignored
- Custom package sources (e.g., `package@user/channel`) are skipped
- Version comparison uses semantic versioning (major.minor.patch)
- The script filters out non-standard version formats from CCI (e.g., "3.1w")
