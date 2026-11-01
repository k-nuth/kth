# Contribution admission

Internal pull requests continue automatically. External contributions wait for a
maintainer to admit CI and request a CodeRabbit review separately. This is an
admission policy, not preemption: it does not cancel jobs already running or
reserve hosted runner slots after an external run has been approved.

## Activate after merging

1. With a repository administrator's authenticated GitHub CLI, run:

   ```sh
   bash scripts/configure_contribution_admission.sh --apply
   ```

   This sets and reads back `all_external_contributors` for **k-nuth/kth only**.
   Merging YAML alone does not change this repository setting. Organization or
   enterprise policy can impose stricter requirements. The script stops on an
   API error or a different returned policy; it does not alter tokens, secrets,
   billing, or workflow write-token permissions.

2. The metadata-only `Classify pull request` workflow assigns `maintainer-pr` to
   authors whose GitHub association is `OWNER` or `MEMBER`, and `contribution` to
   others. It never checks out PR code. CodeRabbit automatic review requires the
   positive `maintainer-pr` label, so a new external PR cannot race automatic
   contribution labeling. Do not manually assign this reserved label to external
   PRs. Outside collaborators remain external for automatic review, even if they
   have write permission; GitHub's native CI approval eligibility follows its own
   permission rules.

3. For already-open internal PRs, add `maintainer-pr` when an automatic review is
   wanted, or wait for the next synchronize/reopen/ready event. Existing external
   PRs without this label remain excluded from automatic review. A CodeRabbit
   review already in flight is not canceled by this change.

## Admit an external contribution

- **CI:** inspect the proposed workflow changes, then select GitHub's **Approve
  workflows to run** for the desired PR/run. Leave others awaiting approval while
  internal work has priority. A CodeRabbit command does not approve Actions.
- **Review:** an organization member comments `@coderabbitai review` after the
  desired head is ready. The automatic-review label filter is separate from this
  manual request. `chat.allow_non_org_members: false` restricts comment
  interactions to organization members; it does not block members from reviewing
  an external author's PR. Do not enable paid review continuation implicitly.
- **Merge:** admission and successful checks do not authorize a merge.

New pushes may require new CI approval. Always inspect current-head CI and actual
CodeRabbit findings; neither an earlier review nor the classification label means
that the current commit is ready to merge. Labels are permissions for automation,
not a substitute for a review result.

## Verify after activation

Confirm the script's read-back succeeds. On a new external fork PR, verify Actions
waits for approval and CodeRabbit does not start automatically. Verify an external
comment cannot request a review, then verify a member's manual request can. On a
new organization-authored PR, verify classification adds `maintainer-pr` and
CodeRabbit starts automatically. These service-side checks require the policy to
be merged and active; local validation cannot establish bot enforcement.

References:
- https://docs.github.com/en/rest/actions/permissions#set-fork-pr-contributor-approval-permissions-for-a-repository
- https://docs.github.com/en/actions/how-tos/manage-workflow-runs/approve-runs-from-forks
- https://docs.coderabbit.ai/reference/configuration
