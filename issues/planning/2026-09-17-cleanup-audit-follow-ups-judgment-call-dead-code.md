---
id: 265N
type: work
status: ready
labels: [bug]
rank: zzzi
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Cleanup audit follow-ups: judgment-call dead code, deprecated spike alias, unscanned areas

## Request
look for cleanup opportunities

## Tasks

- [ ] Decide BoardModel::allLabels()/allIds() (src/BoardModel.cpp:404,415) — uncalled; keep as library API or remove <!-- t:6d -->
- [ ] RemoteShare::stopAll() (src/RemoteShare.cpp:228) has no call site — check whether quit leaks share sessions; wire it into shutdown or remove <!-- t:fj -->
- [ ] Remove RELAY_BUILD_ENGINE_SPIKE deprecated alias (CMakeLists.txt:198) and consider renaming the relay-vterm-spike binary (engine/CMakeLists.txt:107) <!-- t:yk -->
- [ ] Delete docs/ENGINE-SPIKE.md redirect stub if nothing links to it, or keep and leave as-is <!-- t:p5 -->
- [ ] Verify engine/pty/PtyUnix.cpp:~395 post-EOF waitpid(WNOHANG) loop has a sleep/backoff <!-- t:6s -->
- [ ] Verify src/Voice.cpp QProcess error-signal connections (Voice.h:118) <!-- t:tp -->
- [ ] Second audit pass over unscanned areas: RelayWindow methods in main.cpp, unused #includes in largest files, unused config keys, engine/ internals (GhosttyCore, TerminalSession, TerminalView beyond noted items), Python dead code in backend/remote/rendezvous <!-- t:v6 -->
