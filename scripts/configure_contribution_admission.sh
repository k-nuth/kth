#!/usr/bin/env bash
# Apply the repository setting that cannot be expressed in workflow YAML.
set -euo pipefail
if [[ $# -ne 1 || "$1" != "--apply" ]]; then
  echo "Usage: $0 --apply" >&2
  echo "Requires repository administration permission for k-nuth/kth." >&2
  exit 2
fi
endpoint="repos/k-nuth/kth/actions/permissions/fork-pr-contributor-approval"
gh api --method PUT "$endpoint" -f approval_policy=all_external_contributors
actual=$(gh api "$endpoint" --jq .approval_policy)
if [[ "$actual" != "all_external_contributors" ]]; then
  echo "Unexpected effective fork approval policy: $actual" >&2
  exit 1
fi
printf '%s\n' "Verified: every external fork contributor requires workflow approval."
