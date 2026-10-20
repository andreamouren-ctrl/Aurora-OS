# Aurora OS — GitHub Actions runner blocker (2026-10-20)

## Evidence
- Diagnostic PR #223, head `cbf2cf457b0a2bedd8c1a6bfcd251f6056c68b46`, run [39384515561](https://github.com/andreamouren-ctrl/Aurora-OS/actions/runs/39384515561): both independent jobs failed.
- Job `118501031735` (`runner-without-checkout`) has **no checkout, repository access, dependencies or build steps**, only a shell command. GitHub reports completed/failure, steps API `[]`, and logs API returns HTTP 404 `BlobNotFound`.
- Job `118501031460` (`runner-with-checkout`) also completed/failure.
- Other PR #223 workflows, including Bootstrap Build and Identity/Entropy checks, failed at the same time.
- Previous PR #221 Bootstrap Build rerun job `118459509764` failed with steps `[]` and log retrieval `BlobNotFound`.
- Separate development container: `git ls-remote https://github.com/andreamouren-ctrl/Aurora-OS.git HEAD` fails with `Could not resolve host: github.com`. This is an **independent network/DNS restriction**, not proof of the Actions root cause.

## Interpretation
The no-checkout failure excludes Aurora source compilation and repository checkout as **necessary causes** for that diagnostic job. Missing steps/logs suggest failure before ordinary step logging or inaccessible logs, but **do not establish** a specific root cause. Billing, spending limits, Actions policy, runner provisioning and service incidents remain hypotheses.

## Next checks requiring repository owner UI
1. Open [diagnostic run](https://github.com/andreamouren-ctrl/Aurora-OS/actions/runs/39384515561) and capture the **exact red banner/error text** from the workflow page and failed job (not just the red X).
2. Open repository **Settings → Actions → General**: verify Actions is enabled and GitHub-hosted runners are permitted. Do not weaken security or enable untrusted third-party actions indiscriminately.
3. Open account **Settings → Billing and licensing** and review Actions spending limits, payment problems and included-minute usage; availability depends on account/plan. Never share payment details or secrets.
4. Check [GitHub Status](https://www.githubstatus.com/) for Actions incidents at the run time.
5. If no banner explains the failure, open GitHub Support ticket with run ID `39384515561`, job IDs, timestamps, missing steps, and `BlobNotFound`. No code change should be offered as a verified fix until the service reports a cause.

## Acceptance gate
- First get `runner-without-checkout` to pass with marker `AURORA_CI_RUNNER_FINISHED`.
- Then `runner-with-checkout` passes with marker `AURORA_CI_CHECKOUT_OK`.
- Only then retry Bootstrap Build and QEMU acceptance on the exact PR head.

No production code changed, no main merge, no CI root cause claimed.
