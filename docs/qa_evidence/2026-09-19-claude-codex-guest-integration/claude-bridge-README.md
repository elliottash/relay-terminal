# The Claude IDE bridge on a live pane (GT7X, child `claude-bridge`)

Evidence for §26.5's bridge, driven through the built `relay` on an Xvfb screen. This harness is
claude's side of the integration, not Relay's: it finds the sidecar the way a real claude does —
the `<port>.lock` the sidecar writes under `~/.claude/ide` — and speaks the WebSocket variant of
MCP from there, with the `x-claude-code-ide-authorization` header and masked client frames:
`initialize`, `tools/list`, and `tools/call openDiff`, the blocking one. What the pictures are
evidence of is the whole round trip: the call arrives as a `bridge` event through the guest
channel (§26.3, written by `shell/guest-event.py`), the pane opens Relay's diff view beside
itself, and the decision returns the tool call: `FILE_SAVED` after the sidecar has written the
file, or `DIFF_REJECTED` with the file untouched.

**Where the decision is** changed on 2026-09-19 (owner's decision, §26.5): it is `Accept` and
`Reject` in the **diff view's own header**, not the pane's banner. The pictures below were taken
before that and still show the banner's `Save` button; what is under them is unchanged, and the
paragraph at the end of this file says what a re-shoot would show instead and why it has not been
taken here.

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
and what is on disk afterwards. The clicks are found from tesseract's word boxes. Since the
decision moved into the diff view (§26.5) that is much simpler than it was: the two
buttons' labels are one unambiguous word each (`Accept`, `Reject`) and nothing else on the screen
says either, so the word *is* the button and `decision_geometry()` takes the rightmost match. The
banner's old single button could not be found that way — its label carried the shortcut, and the
same pixels OCR'd as `Save` one run and `See` the next — so the code that found it by geometry
(the rightmost full-sized word of the `proposes changes` row, left of the diff pane's edge, with a
height bound to keep the tab row out) is gone with it.

## What each picture is evidence of

`implementer-claude-bridge-01-opendiff-banner.png` — the first `openDiff` on screen: the banner reads
`claude proposes changes to haiku.txt`, the diff pane beside the terminal shows
`claude's haiku +1 -1` with `-the ink dries slowly / +the ink dries fast`, and the call is
blocked while it waits. The tool call has not returned yet at this point — the harness clicks
Save mid-call, which is the point of the picture: the click *is* the reply.

`implementer-claude-bridge-02-saved.png` — Save was clicked and the call returned `FILE_SAVED`; the toast
reads `Saved claude` and the banner is gone. The file on disk now reads `the ink dries fast` —
written by the sidecar, and only after the click, which is the ordering §26.5 promises: the
guest's proposal never touches disk before the user answers.

`implementer-claude-bridge-03-second-diff.png` — a second `openDiff` on the same file (`the page stays
blank`), banner back, call blocked again. The harness clicks the banner's × this time.

`implementer-claude-bridge-04-rejected.png` — the × was clicked and the call returned `DIFF_REJECTED`; the
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

## Reviewed again, 2026-09-19 (second pass)

The pictures above still stand; the code under them changed in five places, all unit-tested in
`tests/test_guest_bridge.py` and `tests/guestbridge_test.cpp` rather than re-shot:

* the shell's `CLAUDE_CODE_SSE_PORT` / `ENABLE_IDE_INTEGRATION` are now **cleared** when the
  bridge is off or has no port (`qputenv` writes the GUI's own environment, so not-setting them
  left the last port behind for every later pane);
* `shell/guest-event.py` exits 1 when a pane was named and the spool write failed, which is the
  exit code `openDiff`'s "failed emit is a DIFF_REJECTED" rule was already reading;
* the sidecar's router prefers a pane with a guest in its foreground over one without, so a diff
  does not open beside the terminal the user is *not* running claude in;
* the frame layer refuses reserved bits, an oversized or fragmented control frame, a stray
  continuation and an unknown opcode (1002), as `remote/ws.py` does;
* a non-ASCII auth header is a 401 rather than a TypeError and a dropped socket.

## The decision moved into the diff view (owner, 2026-09-19)

A pane has **one** banner and it belongs to nobody in particular: an out-of-memory notice, a shell
error or an ssh offer replaces whatever is in it. While the guest diff's Save *was* that banner,
any of those took the only way to accept the change away, and claude then waited out the 30-minute
timeout on a question the user could no longer answer. The other direction was as bad: Ctrl+Shift+R
runs the visible banner's action, so a diff banner made that key write a file instead of restarting
a stopped shell.

So `Accept` (FILE_SAVED) and `Reject` (DIFF_REJECTED) are now two focusable buttons in the diff
view's own header (`DiffView::setDecision`, `tests/diffview_test.cpp`), and the banner is a pointer
at that pane with no action of its own — free to be replaced, and `hideBanner()` settles nothing.
What settles a diff besides the buttons: a new diff replacing the one on screen, the view closing,
and every path that already did (the pane closing, the connection dropping, `close_tab`,
`closeAllDiffTabs`, the timeout, shutdown). The `guest.diffSave` shortcut hint went with the
banner's action.

**The pictures here were not re-shot, and this is why.** `claude-bridge-drive.py` is updated for the
new buttons, but it cannot get past its own setup on `main` today: it types
`!bash -c 'exec -a claude sleep 900'` into the composer and presses Return, and the line stays in
the composer — the `! terminal` chip lit, nothing sent, so no `claude` ever reaches the pane's
foreground and the run stops before the first `openDiff`. That is **not** this change: the same run
fails identically with the decision reverted to the banner (checked, same build tree, 2026-09-19),
with "The agent worker exited" up in the banner both times. Enter's routing was being changed on
`main` the same day (`issues/changes/2026-09-19-ctrl-enter-should-send-now-not-join-the-queue.md`).
Once a Relay on `main` sends a composer line again, the harness re-takes all four pictures
unchanged except for the decision: picture 01 shows `Accept` and `Reject` in the diff pane's header
beside `claude's haiku +1 −1`, the banner beside it reads
`claude proposes changes to haiku.txt · Accept or Reject in the diff pane` and carries no button,
and picture 03 is answered with `Reject` rather than the banner's ×.

## The four owner decisions of 2026-09-19 (third pass)

The pictures above still stand. Four things under them changed, and each is unit-tested in
`tests/test_guest_bridge.py` rather than re-shot, because none of them is visible in a screenshot:

* **Two claudes in one project are told apart.** The sidecar identifies the process on the other
  end of each connection — peer address and port → `/proc/net/tcp` → socket inode → `/proc/*/fd`
  → pid → ancestry → a registered pane shell pid, which panes now register. `openDiff`, `openFile`
  and `close_tab` route by that pane; `closeAllDiffTabs` stays per connection. When the walk cannot
  be made (hidepid, not Linux, a shell that has not named its pid yet) the old ranking decides, and
  the log says which did (`routed … by=peer` / `by=ranking`). The walk takes its `/proc` root as an
  argument, and the tests build one: a hit, a miss, an ancestor two levels up, a v4-mapped v6 peer.
* **The spool write is in-process.** `Bridge.emit` imports `shell/guest-event.py` the way
  `relay_core.guest_hook` does and calls `write_event()`; it used to `subprocess.run` it with a
  ten-second timeout **on the event loop**, so a slow interpreter start stopped every claude on the
  sidecar. A failed write is still a failed emit: `openDiff` answers `DIFF_REJECTED` and `openFile`
  now answers an error instead of claiming it opened the file.
* **The WebSocket is `remote/ws.py`.** The duplicated frame reader and writer are gone; the
  conformance cases above (unmasked → 1002, oversized → 1009, reserved bits, fragmented control
  frames, unknown opcode, handshake timeout) now run against the shared implementation. ws took a
  per-connection `max_frame` (the remote sessions keep its 2 MiB default, the bridge asks for 4 MiB)
  and its `WebSocketError` a close code; the auth-header check stays on the bridge side of the
  handshake.
* **The decision is in the diff view**, as above, with its own tests in
  `tests/diffview_test.cpp` (7 new slots: answered once and once only, a replacing diff and a
  closing view both rejecting, `clearDecision` answering nothing, the buttons focusable).
* **A lock is stale when its port refuses, not when its pid is gone.** After a crash the pid is
  recycled and the old test called a dead editor live, so claude kept dialling a dead port forever.
  The sweep connects to the port on loopback; a port that answers keeps its lock however wrong the
  pid looks, and a port that refuses loses it. The lock records `pidStartTime` for the one case the
  probe cannot answer. Tests use a real listening socket and a real closed port.

## Not covered here

The envelope, the tool set, lock-file lifecycle, stale-lock sweeping, peer identification, pane
routing, the path rule for what the sidecar will write, and the read loop's non-blocking settles
(`close_tab` withdrawing a diff, the 30-minute expiry) are unit-tested instead
(`tests/test_guest_bridge.py`, 117 tests), since they are about files, sockets and `/proc` rather
than pixels. A real Claude Code on the other end of the socket remains for integration with
upstream's client. Two claudes sharing one sidecar is served: `closeAllDiffTabs` and a dropped
connection touch only their own diffs, and two claudes **in one project** are now routed apart by
the peer walk — what is left uncovered there is the fallback, a host where that walk cannot be made
at all, where the newest registration still wins and the diff can open beside the wrong pane
(26.5).
