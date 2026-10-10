#!/usr/bin/env python3
"""Conservative one-off GitHub cleanup: delete only fully merged, unprotected branches.

Safety:
- dry run by default (set --apply)
- never remove default/protected/release/hotfix branches or open-PR heads
- only delete when compare(main...branch).ahead_by == 0
- require compare head SHA == listed SHA, then verify ref SHA just before delete
- preserve *all* branches containing commits absent from default
- write an auditable summary; fail closed on API errors
"""
import argparse
import json
import os
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


def api(repo, token, endpoint, method="GET"):
    url = "https://api.github.com/repos/" + repo + endpoint
    req = Request(url, method=method, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": "Bearer " + token,
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "aurora-os-branch-maintenance/1.0",
    })
    for attempt in range(3):
        try:
            with urlopen(req, timeout=30) as response:
                raw = response.read()
            return json.loads(raw) if raw else {}
        except HTTPError as error:
            if error.code in (429, 502, 503, 504) and attempt < 2:
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f"GitHub HTTP {error.code} on {method} {endpoint}") from error
        except URLError as error:
            if attempt < 2:
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f"GitHub request failed for {endpoint}: {error}") from error
    raise RuntimeError("unreachable")


def paged(repo, token, route):
    all_items = []
    for page in range(1, 30):
        part = api(repo, token, f"{route}{'&' if '?' in route else '?'}per_page=100&page={page}")
        if not isinstance(part, list):
            raise RuntimeError(f"Unexpected paginated response: {route}")
        all_items += part
        if len(part) < 100:
            return all_items
    raise RuntimeError(f"Pagination guard reached: {route}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--apply", action="store_true", help="delete verified integrated branches")
    a = p.parse_args()
    token = os.environ.get("GITHUB_TOKEN", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not token or "/" not in repo:
        p.error("Requires GITHUB_TOKEN and GITHUB_REPOSITORY")
    details = api(repo, token, "")
    default = details["default_branch"]
    if default != "main":
        raise RuntimeError("Safety gate: default branch is not expected 'main'")
    branches = paged(repo, token, "/branches")
    prs = paged(repo, token, "/pulls?state=open")
    open_heads = {pr["head"]["ref"] for pr in prs
                  if pr.get("head", {}).get("repo")
                  and pr["head"]["repo"]["full_name"] == repo}
    outcomes = {"deleted": [], "eligible_dry_run": [], "unique_commits": [],
                "protected": [], "open_pr": [], "uncertain": []}
    for branch in branches:
        name = branch["name"]
        if (name == default or branch.get("protected") or
                name in ("master", "develop", "staging") or
                name.startswith(("release/", "hotfix/", "stable/"))):
            outcomes["protected"].append(name)
            continue
        if name in open_heads:
            outcomes["open_pr"].append(name)
            continue
        encoded = quote(name, safe="/")
        try:
            cmp = api(repo, token, "/compare/" + quote(default, safe="") +
                      "..." + quote(name, safe=""))
            ahead = cmp.get("ahead_by")
            current_head = cmp.get("head_commit", {}).get("sha")
            listed_head = branch.get("commit", {}).get("sha")
            if (ahead != 0 or cmp.get("status") not in ("behind", "identical")):
                outcomes["unique_commits"].append(f"{name} (ahead={ahead})")
                continue
            if not current_head or current_head != listed_head:
                outcomes["uncertain"].append(name + " (moved during compare)")
                continue
            if not a.apply:
                outcomes["eligible_dry_run"].append(name)
                continue
            # Recheck for a moving branch; no stale ref may be deleted.
            actual = api(repo, token, "/git/ref/heads/" + encoded)
            if actual.get("object", {}).get("sha") != listed_head:
                outcomes["uncertain"].append(name + " (moved before delete)")
                continue
            api(repo, token, "/git/refs/heads/" + encoded, method="DELETE")
            outcomes["deleted"].append(name)
        except Exception as exc:
            outcomes["uncertain"].append(f"{name}: {exc}")

    summary = [
        "# Aurora OS — conservative branch cleanup",
        f"Repository: {repo}",
        f"Default branch: {default}",
        f"Mode: {'APPLY' if a.apply else 'DRY RUN'}",
        f"Branches enumerated: {len(branches)}",
    ]
    for key, title in [
        ("deleted", "Deleted fully merged branches"),
        ("eligible_dry_run", "Would delete (dry-run)"),
        ("unique_commits", "Preserved: unique commits"),
        ("open_pr", "Preserved: open pull request"),
        ("protected", "Preserved: protected/default/release"),
        ("uncertain", "Preserved: errors, races or uncertain status"),
    ]:
        items = outcomes[key]
        summary += [f"## {title} ({len(items)})"] + [f"- {name}" for name in items]
    report = "\n".join(summary) + "\n"
    print(report)
    report_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if report_path:
        with open(report_path, "a", encoding="utf-8") as stream:
            stream.write(report)
    with open("branch-cleanup-report.md", "w", encoding="utf-8") as stream:
        stream.write(report)
    if outcomes["uncertain"]:
        print("WARNING: Some branches were preserved due to errors/uncertainty.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
