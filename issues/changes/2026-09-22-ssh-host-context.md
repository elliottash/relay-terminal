---
id: S7CX
type: work
status: needs-verification
labels: [bug, ssh, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
priority: 2
rank: mssh3
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [7102b5071aaedd981421dbf4f1d873c29437deff], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/, docs/qa_evidence/2026-09-22-ssh-delivery/, docs/qa_evidence/2026-09-22-verify-S7KC/], related: [SHPA, S5SH, S7GX, S7KC], github: null}
---
# SSH composer uses the remote filesystem and verified host identity

## Issue
great, add these issues to a card or cards so we can then deliver them

look at how relay deals with ssh. i am noticing the line breaks before and after commands arent there. that indicates to me that some of the main features / functionality isnt working in ssh. 

i also dont want it to be showing cyan "relaying - ssh..." the whole time. there should be a clear indicator for ssh, but it shouldnt be the general shell command indicator. 

first analyze the code and find bugs and issues to reach parity with the non-ssh terminal. then live drive it to check for any UX issues

## Planning notes
Confirmed: shot 04b in `docs/qa_evidence/2026-09-22-ssh-parity/` shows Tab completing a file present only in the local workspace while the remote shell is in /tmp. Shot 04 shows the composer directory chip still naming the local workspace after remote cd.

`src/Pane.h`: completeInComposer uses local m_cwd/knownCommandNames; resolveComposerPath, attachmentsFor and the @ picker use local QFileInfo. Local-only attachments are a code-established gap, not a reproduced upload. Remote cwd is stored separately in m_login.cwd and should not overwrite local project context.

Code-only risk requiring two-host reproduction: beginLogin binds the outer SSH process/control socket; onCwdHostChanged accepts remote OSC 7 paths but discards the host while logged in. Nested SSH may change where the visible terminal is typing without rebinding agent tools. A host announcement alone is not proof of a reusable authenticated connection.

Third delivery priority, but destination guards should coordinate early with #S7GX. Independent completion/display work can proceed alongside #S7KC; do not let tools act on a stale outer-host identity while claiming to target an inner host.

## Done means
- The composer clearly displays SSH host and remote cwd, updating after cd and restoring local context after exit; local project paths are separately identified.
- Tab path/command completion and suggestions use remote context and remain responsive. Missing remote capability is explicitly unavailable rather than falling back to local names.
- @ picking and relative attachments resolve on the intended host, including identical local/remote filenames and remote-only files; content and provenance identify the actual source.
- A reproduced two-host nested SSH transition cannot send agent/file operations to the outer host under an inner-host label. Verify and bind each destination or explicitly disable affected tools until it is known; unwind/disconnect cannot retain stale bindings.
- Regression and live evidence cover remote-only/local-only paths, changing cwd, delayed/disconnected lookup, nested hosts, reconnect and local restoration, with no unintended local-file attachment.

## Tasks
- [x] Make displayed directory and completion/suggestion lookups use explicit local versus remote context. <!-- t:6w -->
- [x] Implement remote-aware @ lookup and attachment provenance using existing remote file operations. <!-- t:x0 -->
- [x] Reproduce nested SSH across two distinct destinations; add identity/capability guards and supported transition handling. <!-- t:r3 -->
- [ ] Add collision/latency/disconnection tests and record live GUI/tool evidence for remote paths and host transitions. <!-- t:je s=in-progress -->

## Plan
**Goal:** Deliver the existing Done means with live evidence.
**Findings:** See Planning notes and #SHPA; the audited paths are unchanged.
**Steps:** Bind remote identity/cwd independently of local project state, guard nested identity changes, and make composer completion, suggestions and attachments remote-aware. Test remote/local filename collisions and stale/disconnected asynchronous lookups, then live-drive the GUI.
**Risks:** Shared checkout; preserve other work. Remote and local command ownership must remain separate; do not route stale remote requests locally. No permanent remote shell startup changes.
**Verify:** Targeted regression tests plus isolated Xvfb/real localhost SSH; record precise limits of guest and platform testing. Independent verifier reviews acceptance and live evidence after implementation.

## Execution Summary
Added remote host/cwd display, asynchronous bounded Tab/@ queries over the authenticated socket, host-scoped history suggestions, and explicit-host remote attachment loading with provenance. Per-login shell confirmation revokes host capabilities during command/nested-shell ownership and restores them only at the confirmed original prompt. Live context updates reach ongoing worker turns; lookup results are discarded after edits/cd/reconnect. Nested SSH is conservatively unavailable to host tools rather than misdirected to the outer connection.

## Tests
`tests/test_attachments.py`
`tests/test_guest_board_bridge.py`
`tests/test_ssh_remote.py`
`ctest --test-dir build -R '^(completion|remotefiles|remotesession)$' --output-on-failure` — 3 passed.
manual: docs/qa_evidence/2026-09-22-verify-S7GX/
manual: docs/qa_evidence/2026-09-22-ssh-delivery/
manual: docs/qa_evidence/2026-09-22-verify-S7KC/

### Check 2026-09-22 11:19
- passed · unittest:tests.test_attachments — tests/test_attachments.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- passed · unittest:tests.test_guest_board_bridge — tests/test_guest_board_bridge.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- passed · unittest:tests.test_ssh_remote — tests/test_ssh_remote.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-S7GX/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-S7GX/
- not-applicable · manual:docs/qa_evidence/2026-09-22-ssh-delivery/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-ssh-delivery/
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-S7KC/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-S7KC/
- notice · unittest:tests.test_ssh_remote — tests/test_ssh_remote.py: 1 of 46 are slow (test_handed_back_jobs_carry_the_host)
history: thread

## QA checklist
- [x] Independent GUI drive verifies host/cwd display after cd and local restoration after SSH exit; local-only filename does not complete remotely, remote-only filename and host-labelled @ collision picker do.
- [x] Custom interactive remote PATH completion now passes on 11H.05 after `0b4b1798`: remote_only_p completes to remote_only_probe after exporting the remote fixture bin directory.
- [x] A real remote completion query delayed two seconds completes printen to printenv; replies after draft edit and after SSH disconnect are discarded without stale insertion or popup (`delayed7/`).
- [x] Removing availability of the isolated control socket explicitly refuses remote completion without local fallback (`build7/16-missing-socket-lookup.png`). Restoring local context restores local filename completion.
- [x] Real nested SSH localhost -> 127.0.0.1 withholds control_path and reachable until original shell returns; exit clears remote context. These are distinct SSH destinations/authenticated sessions on one machine, not two physical hosts.
- [x] a1's separate backend evidence covers remote/local attachment collision content and session guards: `docs/qa_evidence/2026-09-22-verify-S7GX/`.
- [ ] Suggestions were reviewed statically but not exercised through a provider; two physical hosts and network packet-loss conditions were not available in this run.

## Verdict
Independent a2: **passes the exercised host-context, interactive PATH, stale lookup and destination-guard checks.** `docs/qa_evidence/2026-09-22-verify-S7KC/build7/README.md` and `delayed7/README.md` record real SSH plus a two-second compgen shim; no paid model CLI. The initial PATH mismatch is fixed in `0b4b1798`. Scope limits above remain explicit; no claim that same-machine aliases establish every cross-machine identity case. Status unchanged.
