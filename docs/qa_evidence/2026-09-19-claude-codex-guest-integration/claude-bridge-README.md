# The Claude IDE bridge on a live pane (GT7X, child `claude-bridge`)

Evidence for §26.5's bridge, driven through the built `relay` on an Xvfb screen. This harness is
claude's side of the integration, not Relay's: it finds the sidecar the way a real claude does —
the `<port>.lock` the sidecar writes under `~/.claude/ide` — and speaks the WebSocket variant of
MCP from there, with the `x-claude-code-ide-authorization` header and masked client frames:
`initialize`, `tools/list`, and `tools/call openDiff`, the blocking one. What the pictures are
evidence of is the whole round trip: the call arrives as a `bridge` event through the guest
channel (§26.3, written by `shell/guest-event.py`), the pane opens Relay's diff view beside
itself and shows the banner whose Save button — or whose × — is the decision, and only that
click returns the tool call: `FILE_SAVED` after the sidecar has written the file, or
`DIFF_REJECTED` with the file untouched.

## Running it

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 claude-bridge-drive.py <output-dir>

`claude-bridge-drive.py` starts the worktree's `build/relay` under a fresh temp root that holds
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, so nothing of the
user's session is read or written. `RELAY_DATA_DIR` is the worktree, so the sidecar, the helper
and the backend under test are this build's. `RelayTerminal/relay.conf` carries
`[instructions] onboarded=true` and `[hints] enabled=false` so first-launch and hint toasts stay
out of the pictures, and `[guests] claude_bridge=true` — the one thing under test that is off by
default (§26.5) — is the one thing it turns on.

Real Claude Code is not installed, so the harness puts a stand-in of that name in the foreground
the same way the hooks harness does: `!bash -c 'exec -a claude sleep 900'` typed into the
composer, making the pane's foreground `cmdline` read `claude` for `guestProgram()`'s
classifier. The pane the harness waits for is the one whose shell wrote `state.json` — the
terminal pane, in other words; Relay makes a runtime dir for the file pane too, and picking the
sorted-first name instead waits forever on the one without a shell.

Each picture is verified, not just taken: the harness OCRs the screenshot and logs what the
window says, and the hard assertions are what a guest depends on — what the tool call returned,
and what is on disk afterwards. The clicks are found from tesseract's word boxes, but never by
what the words say: the same button pixels have read `Save` and `See`, so the geometry is what
is trusted. The banner row is the one holding `proposes changes`; the pane's right edge is the
first tall thin column right of that text (the diff pane opening beside makes one); the Save
button is the row's rightmost full-sized word — any point of its label is the button, and the
label is wider than its `Save` word — and the × is the tiny glyph past the label's end, or 32 px
past it when OCR drops the glyph. Tab-row noise OCRs as tall words that would claim to be the
button; the row filter's height bound keeps them out.

## What each picture is evidence of

`claude-bridge-01-opendiff-banner.png` — the first `openDiff` on screen: the banner reads
`claude proposes changes to haiku.txt`, the diff pane beside the terminal shows
`claude's haiku +1 -1` with `-the ink dries slowly / +the ink dries fast`, and the call is
blocked while it waits. The tool call has not returned yet at this point — the harness clicks
Save mid-call, which is the point of the picture: the click *is* the reply.

`claude-bridge-02-saved.png` — Save was clicked and the call returned `FILE_SAVED`; the toast
reads `Saved claude` and the banner is gone. The file on disk now reads `the ink dries fast` —
written by the sidecar, and only after the click, which is the ordering §26.5 promises: the
guest's proposal never touches disk before the user answers.

`claude-bridge-03-second-diff.png` — a second `openDiff` on the same file (`the page stays
blank`), banner back, call blocked again. The harness clicks the banner's × this time.

`claude-bridge-04-rejected.png` — the × was clicked and the call returned `DIFF_REJECTED`; the
toast reads `Kept the file` and the file on disk still reads `the ink dries fast`. A refused
proposal changes nothing.

## The bug the first run caught

The first run of this harness timed out with the banner up and Save clicked: the click had
worked, the pane had called the bridge's answer path, and the answer was refused every time.
`answerDiff` checked the reply file with `QFileInfo::canonicalFilePath()`, which is empty for a
file that does not exist yet — and the reply file is exactly the file the call is about to
write, so it never exists yet; the inside-the-runtime-dir check refused its own reply
(`guest_bridge_reply_refused`), and the tool call never returned. The fix is `replyAllowed()` in
`src/GuestBridge.h`: the directory is canonicalized (the runtime dir exists), the reply path is
compared as `QDir::cleanPath(QFileInfo(path).absoluteFilePath())`, which both works for a file
that does not exist and closes a `..` traversal that `cleanPath` alone would not have caught.
`tests/guestbridge_test.cpp` unit-tests the rule, including
`aReplyThatDoesNotExistYetIsAllowed` — the regression this run found.

## Not covered here

The envelope, the tool set, lock-file lifecycle, stale-lock sweeping, pane routing, the path
rule for what the sidecar will write, and the read loop's non-blocking settles (`close_tab`
withdrawing a diff, the 30-minute expiry) are unit-tested instead
(`tests/test_guest_bridge.py`, 83 tests), since they are about files and sockets rather than
pixels. Two claudes sharing one sidecar, and a real Claude Code on the other end of the socket,
remain for integration with upstream's client.
