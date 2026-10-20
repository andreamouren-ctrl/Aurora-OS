# Aurora OS — Repository cleanup audit (2026-10-20)

Status: **first-pass inventory; no deletions authorized by evidence yet**.
Baseline: `main` at `b3575500d94eca59d55d45a492ba292ff53f069f`.

## Goal and constraints
Remove demonstrably unnecessary tracked files while preserving build inputs, runtime assets, recovery paths, subsystem contracts, test fixtures, ADR history and evidence needed for acceptance. Keep all destructive edits isolated in a reviewable PR; never delete directly from `main`.

## First-pass observations
- `.gitignore` already excludes `build/`, `out/`, `dist/`, object/library/ELF/ISO/image/bin outputs, downloaded `third_party/`, `.cache/`, editor metadata, logs, and selected generated runtime assets.
- GitHub indexed-code searches returned **no results** for `extension:log`, `extension:tmp`, `extension:bak`, `extension:orig`, `extension:rej`, `extension:zip`, `extension:pyc`, `extension:swp`, `extension:old`, `extension:patch`, `extension:diff`, `extension:map`, `extension:class`, `extension:sqlite`, `extension:db`, `filename:Thumbs.db`, `filename:desktop.ini`, `path:build/`, and `path:.cache/`.
- **Caveat:** GitHub code search is indexed and may omit unindexed files, oversized files, binaries, or ignored content. These negative results do **not** prove that the repository tree is clean. A complete recursive tracked-file manifest is still required.
- `docs/README.md` explicitly requires preserving historical ADRs and the boot/recovery fallback. Do not remove `legacy` paths based on naming alone.
- `README.md` identifies `assets/fonts/aurora-celestia/ui/v1.0/` as the verified system font release; these assets must not be treated as disposable generated binaries.
- Recent CI jobs on PR #221 failed before retrievable steps/logs. Do not equate a green static audit with validated deletion safety.

## Mandatory full-tree audit before deletions
1. Produce `git ls-files -z` manifest and classify by tracked file type, size, and owner; include binary/hidden files and case-collisions.
2. Identify build dependencies using Makefile includes, generated-asset scripts, linker inputs, CI workflow references, runtime loader paths and tests; search for exact paths and symbol references.
3. For each proposed deletion, record **path, SHA, reason, references checked, runtime/build impact, and restoration strategy**.
4. Delete only files proven redundant (e.g. tracked cache or exact duplicated orphan) in a focused branch. Keep ADRs, docs with active links, golden test data, compatibility paths and recovery assets unless supersession is demonstrated.
5. Verify kernel/ISO build, host tests, boot QEMU and required CI on the deletion commit; cold reboot and negative tests where affected. No merge on unavailable or failing gates.

## Initial disposition
**No file deletions in this pass.** Available connector exposes targeted file fetch and indexed search but has not yielded a complete recursive Git tree; deleting source or assets now would be speculative. This report is a scoped, auditable starting point, not a claim of a complete repository audit.

## Reproducible local inventory tool
- Added `scripts/audit-tracked-files.py` on this audit branch. Run from a **complete checkout** with `python3 scripts/audit-tracked-files.py --output tracked-inventory.json` (do not commit the output unless reviewed).
- The script reads NUL-delimited `git ls-files --cached`, checks tracked paths, hashes present regular files, and reports byte-identical groups and generated/cache-pattern candidates as **review-only**, never deletes files.
- It reports missing worktree paths separately; do not interpret sparse checkout omissions as safe deletions. Identical content does not establish equivalent runtime roles.
- Tool added but **not yet executed against a full checkout** because this session cannot resolve `github.com` for cloning. No deletion approval or build verification follows from adding the script.
