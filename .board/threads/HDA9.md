<!-- relay:entry 20260919T040032Z-vt author=agent kind=event model=glm-5.3 pane=31a227b5 turn=177747a964a04b96bf946460d55ba6cd/9350972ab23a4087935ff74830afe210 -->
- ✦ agent created this card in In progress · issues/features/2026-09-19-update-download-and-install-the-latest-release-t.md

<!-- relay:entry 20260919T042527Z-ft author=agent kind=event model=glm-5.3 pane=31a227b5 turn=e353a94aa489449bbed84d61244589db/f6d55c8494d74ad79ddea32c7ec1a114 -->
- ✦ agent updated this card · appended to `## Built`

<!-- relay:entry 20260919T042533Z-yf author=agent kind=event model=glm-5.3 pane=31a227b5 turn=e353a94aa489449bbed84d61244589db/f6d55c8494d74ad79ddea32c7ec1a114 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T042537Z-zt author=agent kind=progress model=glm-5.3 pane=31a227b5 turn=e353a94aa489449bbed84d61244589db/f6d55c8494d74ad79ddea32c7ec1a114 -->
Verification finished and evidence recorded in docs/qa_evidence/2026-09-19-update-slash-command/ (drive.sh, run.log, NOTES.txt). While it ran, a concurrent session in this shared tree rewrote src/Pane.h from a pre-/update buffer at 00:20:54 and dropped the three /update hunks; they were restored byte-identically ~00:24 and the build's only remaining errors are that session's own in-flight m_initSnoozed member. Nothing committed: the tree holds several other cards' uncommitted work in the same files. What still needs a packaged machine: the pkexec install and restart path.

<!-- relay:entry 20260919T042540Z-tx author=agent kind=event model=glm-5.3 pane=31a227b5 turn=e353a94aa489449bbed84d61244589db/f6d55c8494d74ad79ddea32c7ec1a114 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · /update verified: 8/8 unit tests, clean build with the wiring, live GitHub discovery + download + SHA256SUMS dry-run; install/restart path needs a .deb machine · evidence docs/qa_evidence/2026-09-19-update-slash-command/

<!-- relay:entry 20260919T171442Z-c7 author=agent kind=decision model=anthropic/claude-fable-5.1 -->
Owner decision C7, 2026-09-19: the release channel is an option — default every non-draft release with prereleases included, as before, plus a "stable only" choice that excludes them — and within the channel the **highest** version wins, not the first GitHub lists.

<!-- relay:entry 20260919T171442Z-c8 author=agent kind=progress model=anthropic/claude-fable-5.1 -->
Done. `latest_release()` split into `fetch_releases()` + `pick_release(releases, channel)`; `in_channel()` decides the channel (draft never, prerelease only in "all"); the winner is chosen with the existing `apt_newer()` dpkg helper on `upstream_version(tag)` alone, so the `-1~<slug>` suffix and the distribution play no part. A tag dpkg cannot read is skipped rather than ordered by guess, and a channel holding nothing readable falls back to GitHub's order where `apt_newer` already refuses with "Could not compare …". `--channel all|stable` is on both `check` and `install` (a shared parent parser). Surface: Options › General › Updates › Update channel (`update/channel`), read by `RelayWindow::updateChannel()` and passed to the script by `updateApp()`, so `/update` and the palette's Update action both honour it. `tests/test_update.py` grew `ChannelTest` and `ChannelFlagTest`: 26/26.
