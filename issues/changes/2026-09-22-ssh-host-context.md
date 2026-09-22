---
id: S7CX
type: work
status: inbox
labels: [bug, ssh, terminal]
assignee: null
priority: 2
rank: mssh3
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/], related: [SHPA, S5SH, S7GX, S7KC], github: null}
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
- [ ] Make displayed directory and completion/suggestion lookups use explicit local versus remote context. <!-- t:6w -->
- [ ] Implement remote-aware @ lookup and attachment provenance using existing remote file operations. <!-- t:x0 -->
- [ ] Reproduce nested SSH across two distinct destinations; add identity/capability guards and supported transition handling. <!-- t:r3 -->
- [ ] Add collision/latency/disconnection tests and record live GUI/tool evidence for remote paths and host transitions. <!-- t:je -->
