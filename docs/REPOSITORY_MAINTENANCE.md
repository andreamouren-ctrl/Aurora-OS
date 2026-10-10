# Aurora OS — Repository Maintenance and Branch Retention

Status: **Current repository process**  
Updated: **2026-10-10**

## Canonical source

- **`main` is the only accepted integration baseline.**
- G5 WP-03 and WP-04 are integrated and runtime verified. WP-04 was merged as [PR #185](https://github.com/andreamouren-ctrl/Aurora-OS/pull/185) at `a45d3cab10544371d5f3535af0a768e6e0ea7e47`.
- The next Identity implementation work must branch from current `main`, not from experimental Identity/reauth branches created before the Shell freeze.
- Historical audit/phase notes are snapshots, not current-state overrides. Check the newest dated synchronization notes at the top of the roadmap/audit.
- Future G5 WP-05 (camera/Living Canvas) is **not** a prerequisite for backend Identity work; trusted pre-session compositor Identity hosting remains a separate unfinished integration.

## Branch cleanup safety contract

The maintenance workflow `.github/workflows/maintenance-prune-merged-branches.yml` runs only on the explicitly marked merge commit `[repo-cleanup] Synchronize Aurora docs and prune merged branches`. It uses `scripts/maintenance/prune_merged_branches.py` and records a GitHub Actions report artifact.

1. Enumerate all branches including pagination and all open pull requests.
2. Never delete `main`, protected refs, `master`/`develop`/`staging`, `release/*`, `hotfix/*`, `stable/*`, or a branch used as the head of an open PR in this repository.
3. Check `compare(main...branch)`. Delete only when `ahead_by == 0`, meaning **every commit on the branch already exists in `main`**. This is intentionally stricter than 'merged PR': cherry-picks, squash merges and rebased history may leave unique commits.
4. Verify the branch SHA before deletion. A moved ref or API error is preserved, never guessed safe.
5. Keep diverged/unique-commit branches for a separate review. They may contain useful unmerged research even if very old; their existence is not authorization to merge them over current `main`.
6. The script defaults to **dry-run**; `--apply` is only used by the single-purpose maintenance workflow, with an explicit `contents: write` GitHub Actions token. The workflow uploads the report of deleted and preserved branches.

The maintenance workflow does **not** delete Git tags, GitHub releases, issues, merged PR history, or project documents, and does not rewrite Git history.

## Acceptance

Before treating cleanup as complete: check the report artifact, GitHub Actions result, actual remaining branch inventory, `main` SHA and current documentation. If GitHub Actions write permissions prevent deletion, preserve the branches and report the blocker; do not claim success.

## Documentation normalization

On 2026-10-10, root `README.md`, global `docs/ROADMAP.md`, graphics README/implementation roadmap, Identity implementation roadmap and System App UX, historical audit synchronization, and `services/identity/README.md` were updated to distinguish:

- WP-03/WP-04 **already merged/runtime accepted**;
- WP-05+ **still planned**, not part of the completed window-management slice;
- live Ring 3 Identity/Session services **already integrated**, but **not production certified**;
- compositor-backed **pre-session Identity System App and recovery credential/device flows still pending**.

For identity development, review `docs/identity/IMPLEMENTATION_ROADMAP.md` and `docs/identity/SYSTEM_APP_UX.md` before starting a new branch.

## Bootstrap of the cleanup workflow

The workflow was added by [PR #186](https://github.com/andreamouren-ctrl/Aurora-OS/pull/186). GitHub may not execute a newly introduced push workflow for the same merge event that introduces it. A dedicated follow-up push with the narrowly matched `[repo-cleanup]` marker requests the first cleanup run; the report and actual branch inventory remain the authoritative evidence of deletion.
