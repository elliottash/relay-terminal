---
id: YSGA
type: work
status: done
labels: [bug, packaging]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
resolution: done
discovered_from: 9Y7X
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
links: {plans: [], commits: [0d5ac28e32d7], evidence: [], related: [9Y7X], github: null}
---
# Debian package build omits the required LibLZMA development package

## Issue
A clean Ubuntu 26.04 package build from commit f9c7d590 configures Qt PDF successfully, then fails at CMakeLists.txt:318 because packaging/deb/build-deb.sh does not install liblzma-dev even though relay-windowstate requires LibLZMA. Add the build dependency and verify a clean package build.

## Done means
A clean supported `.deb` build installs LibLZMA headers and passes the CMake configure step that links `relay-windowstate`.

## Tests
manual: /home/elliott/repos/relay-terminal/packaging/deb/build-deb.sh

### Check
PASS — `bash -n packaging/deb/build-deb.sh` passed. A clean Ubuntu 26.04 container installed `liblzma-dev`, configured with `LibLZMA::LibLZMA`, and compiled past the prior CMake failure. The subsequent Qt 6 Board source error is tracked by #3S34.

## Execution Summary
Added `liblzma-dev` to the common `.deb` build dependencies in `packaging/deb/build-deb.sh` (commit `0d5ac28e32d7`).
