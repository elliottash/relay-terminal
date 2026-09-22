# HG26 instrumentation contract

Tool event fields retain session, turn, call, tool, ms, exit_code and ok; add outcome and error_code.
Outcomes: success, pending, refused, command_nonzero, timed_out, transport_error, internal_error, unknown.
error_code is a fixed classifier-owned token, never arbitrary error prose. Normal agent_wait deadlines are pending.
Every worker log line adds origin (interactive/test/qa), run_id, build_id after the event fields (preserving the historical prefix and event position). Missing historical fields remain unknown.
Environment overrides: RELAY_LOG_ORIGIN, RELAY_LOG_RUN_ID, RELAY_BUILD_ID. Invalid identity tokens become unknown.
Ownership: a1 touches only Agent._record_tool and Pane worker-finished logging in shared files.
Relay board/message tools are absent in this guest; parent owns HG26 card/thread updates.

Backend build_id defaults to a cached source-tree SHA-256 fingerprint (source- plus 20 hex digits); packaged/QA runs can supply RELAY_BUILD_ID. GUI worker_exit has origin/run_id/build_id plus expected=0/1 and reason=shutdown/reconfigure/unexpected, using its captured running build identity. Test-runner imports isolate XDG_DATA_HOME before spawning workers; a runner already marked origin=test retains its supplied isolated XDG directory.
