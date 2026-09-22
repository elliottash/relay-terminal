# Independent verification of #S7GX

2026-09-22. Runtime checks pass on the current shared working copy; the previously reported guest-facing guidance inconsistency is fixed. Implementation and card status were not changed. Source SHA-256 values are recorded in `results.json` because the checkout is concurrent and uncommitted.

## Checks performed

- `PYTHONPATH=backend:. python3 -m unittest tests.test_guest_board_bridge tests.test_ssh_remote tests.test_attachments`: **64 tests passed** (`tests.txt`). System Python has no pytest; unittest is the modules' native runner.
- `PYTHONPATH=backend:. python3 docs/qa_evidence/2026-09-22-verify-S7GX/drive.py`: **passed** (`results.json`).
- Created one authenticated localhost OpenSSH master with a private temporary control path. Created fixtures solely in a temporary remote directory and terminated only that master. No user SSH sessions touched.
- Actual JSON-RPC MCP initialize/discovery/calls through the bridge's subprocess proxy and Unix socket, Agent preparation/execution, and real SSH.
- Codex `mcpToolCall` and Claude `mcp__relay_board__...` mappings both used for remote command cwd/output/exit 7 and create/read/edit/list/delete. Delete used run_command because no delete_file tool is exposed.
- Large-file query measured fixtures as 8192 bytes and 37 bytes in descending order.
- No host, wrong host, no session, ended turn, and secret-looking paths all refused. Next turn discovery remained stable.
- Background job returned after one second; stop_command and command_output reported a stopped SSH job. This proves job-process cancellation, not termination of every possible descendant remote process.
- Missing control socket refused before dispatch. An abandoned Unix socket passed the filesystem socket check but SSH exited 255 with connection refused; no sentinel command executed. Remote read likewise failed. Thus the new ProxyCommand=false guard prevented a fresh key-authenticated login even though localhost login works.
- Existing unit coverage checks native board behavior, turn capability revocation, terminal handoff capability gates, and pre-dispatch cancellation.

## Follow-up verification: guest guard and #S7CX attachments

The implementer fixed the previous guidance finding: `remote_session.context_note` now distinguishes native local tools from mandatory-host guest tools, and the no-session error no longer recommends omitting host. The complete real MCP/SSH drive passed again against the new implementation.

`PYTHONPATH=backend:. python3 docs/qa_evidence/2026-09-22-verify-S7GX/attachments_drive.py` passed. This stages its own authenticated localhost master and reads through `session_protocol.load_attachments`, exercising the actual remote loader and SSH subprocess:

- Local and remote `same.txt` contain different bytes: host-tagged attachment returns only remote bytes with remote cwd/host provenance; an explicitly local attachment still returns local bytes.
- A valid 1x1 PNG arrives byte-for-byte as an image. Text above 128 KiB is truncated; a PNG above the 3 MiB image cap is refused.
- Missing remote file is refused despite an existing local file with that name. Binary file, wrong host, no session and unshareable session are refused.
- Removed socket and abandoned socket both refuse attachment reads without a fresh login.
- Independent injected changes after genuine bridge preparation prove both new guards: changing executor remote session and cancelling the turn each refuse before `_execute` is called.

`attachments_results.json` records outcomes and source hashes. Static review confirms `worker.py` validates `remote_session_update` before replacing the executor snapshot, and `session_protocol.load_attachments` supplies that snapshot to the remote loader. Worker message-loop and GUI ordering were not live-driven by this verifier; the parent is driving that integration. No remaining defect found in the requested live cases.

## Limits

This is actual MCP transport plus both adapter naming mappings, using FakeHarness only to instantiate the provider without paid model calls. It is not a live Codex/Claude CLI model session. No GUI handoff lifecycle drive or build was performed; those belong to the parent agent's coordinated SSH verification. The verifier's own harness exposes no relay_board tools, so no board/delegation connection is claimed and no card status was changed.
