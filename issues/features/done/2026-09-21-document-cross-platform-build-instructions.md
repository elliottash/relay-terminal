---
id: B1AD
type: work
status: done
labels: [feature, documentation, packaging]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex user request, 2026-09-21'
links: {plans: [], commits: [d305e473], evidence: [docs/BUILDING.md], related: [R6BS], github: null}
---
# Document cross-platform build instructions for every repository agent

## Issue
document in the readme about how to build, or somewhere that codex, claude, and relay will all see it

## Done means
The repository has one canonical guide for developer, Linux package, native Windows installer and
native macOS DMG builds. Codex, Claude and Relay project instructions all point to it, and the
public README makes it discoverable. Commands match the successful native CI workflows.

## Tasks
- [x] Add one cross-platform build guide with local and CI commands. <!-- t:a1 -->
- [x] Link it from the README and every agent instruction entry point. <!-- t:a2 -->
- [x] Check links, shell examples and documentation formatting. <!-- t:a3 -->

## Execution Summary
Added `docs/BUILDING.md` as the canonical guide for local development, Linux `.deb` packages,
native Windows installers, native Apple Silicon and Intel Mac DMGs, and GitHub Actions builds.
Linked it from the README and every repository-agent instruction entry point. Also corrected the
remaining README and agent-guide descriptions that called Relay Linux-only.

## Tests
- `manual: docs/BUILDING.md`
- `git diff --check -- AGENTS.md CLAUDE.md WARP.md README.md docs/BUILDING.md`
- `bash -n packaging/macos/package.sh packaging/deb/docker-build-all.sh scripts/build.sh`
