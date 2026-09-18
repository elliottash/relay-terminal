# Commands outlive their timeout — implementer evidence

- `tests/test_jobs.py` (15 tests) and the two updated timeout tests in `tests/test_tools.py`: a slow
  command comes back as a job and is read later (only unread output), a wait returns early when
  the job ends, `background: true` returns at once, `stop_command` and a finished shell both take
  the process group's children, Stop ends only the command being waited on, a new conversation
  (`shutdown`) stops every job, timeouts are clamped (5000 → 1800, 0 → 1, "180" → 180, 90.4 → 90,
  "soon" → 30), the newest output is kept, the live stream runs only while a call waits, at most
  8 jobs, and subagents with run_command get the two job tools.
- Full suite: 989 Python tests OK; ctest 32/32.
- `live-run-glm-5.3.txt`: a real turn under Xvfb (isolated XDG dirs), glm-5.3. A 60 s loop with the
  default 30 s wait came back as `job-1`; the model waited on it with `command_output` and got
  `built`; it started `python3 -m http.server 8765` with `background: true`, checked it with curl
  (HTTP 200) and stopped it with `stop_command`. Nothing was listening on 8765 afterwards.
