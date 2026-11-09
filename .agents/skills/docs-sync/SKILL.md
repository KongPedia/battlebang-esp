---
name: docs-sync
description: Audit or update BattleBang ESP firmware documentation and agent guidance against the requested implementation scope.
---

# Docs Sync

## Overview

Compare the requested features, configuration or guidance against the current repository evidence, then identify
coverage gaps and inaccuracies through both doc-first and code-first passes.

## Authorization and scope

For an audit or proposal-only request, report findings without editing. For requested updates or an approved plan,
complete the authorized local edits and static checks without asking again. Ask only about unresolved scope,
behavior or additional authority. Follow applicable `AGENTS.md` instructions; this skill does not authorize application
changes, dependency installation, commits, publishing, services, CI or hardware operations.

## Workflow

1. Confirm the requested topic and current branch
   - Inspect the current branch and working-tree changes; do not switch branches or overwrite others' work.
   - Use an intended-base diff only for branch-scoped requests. Topic corrections remain in scope even if unrelated
     to that diff. Bound a comprehensive inventory to an explicitly comprehensive audit request.
   - Record the inspected revision and distinguish committed code from local changes and proposed behavior.

2. Build an evidence inventory
   - Inspect the owning source, configuration, tests and callers without running application code.
   - Capture each relevant file path and symbol, option, command, default or documented runtime behavior.
   - For guidance work, include root/scoped `AGENTS.md`, `.agents/` entrypoints, role configuration and skill references.
   - Prefer targeted `rg` searches; do not infer runtime support or hardware success from declarations or examples.

3. Doc-first pass: review existing pages
   - Read the relevant human documentation in the owning firmware/library READMEs and `docs/` and agent procedures under `.agents/`.
   - Check each page's claims, missing supported options and expected reader intent against the inventory.
   - Keep human operational instructions separate from agent workflow rules; link to one canonical owner.
   - Preserve the language and tone of the existing page; do not impose an English-only or translation exclusion rule.

4. Code-first pass: map evidence to documentation
   - Find the appropriate existing page or section for each supported feature, setting or workflow.
   - Inspect current indexes and cross-links rather than assuming an SDK documentation generator or navigation file.
   - Prefer updating an existing page over adding another source of truth. If generated content is encountered,
     identify its actual source before proposing edits; do not hand-edit generated artifacts.
   - Separate implemented/current, planned and historical claims. Source and test evidence do not prove deployment
     or physical validation.

5. Detect gaps and inaccuracies
   - **Missing:** supported behavior or guidance absent where readers need it.
   - **Incorrect/outdated:** names, defaults, paths or behavior differ from the inspected evidence.
   - **Structural:** duplicated ownership, broken references or mixed audiences obscure the canonical procedure.
   - Record file/symbol evidence and a suggested insertion point for each finding.

6. Report or apply authorized updates
   - For audit-only work, report evidence, suggested locations and proposed edits, then stop.
   - Otherwise make the smallest authorized documentation/guidance edits, preserving unique safety, approval,
     rollback and validation information. Update indexes, callers and cross-links when moving or renaming pages.
   - Do not broaden prose maintenance into application/runtime configuration changes. Report those separately
     unless the user explicitly included them; they require the repository's firmware verification lane.

7. Verify at the requested layer
   - For prose, prompt and harness-only edits, perform the one-off metadata, reference and hash validation
     described in `.agents/README.md`, parse changed JSON/TOML and review the complete diff with `git diff --check`.
   - Report missing tooling or failed checks honestly; do not introduce a persistent validator or test harness.
   - Do not run application tests, builds, services, CI, deployment or hardware solely to verify prose.
   - Do not claim fresh-session skill discovery or model adherence from static validation.

## Output format

Docs Sync Report

- Doc-first findings: page + missing/incorrect content -> evidence + insertion point.
- Code-first gaps: feature/workflow + evidence -> existing or proposed documentation location.
- Structural suggestions, only when needed: ownership or navigation correction + rationale.
- Applied or proposed edits: file -> concise change summary; identify audit-only proposals explicitly.
- Verification: exact static checks and results, omitted checks, and unresolved decisions or bounded blockers.

## References

- `references/doc-coverage-checklist.md`
