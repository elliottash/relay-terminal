# Expected, #ZPWT Try it (sealed until you have run stage.sh)

Both scenarios must show `exit_code: -9`, `killed_for_memory: true`, empty command output, and a
`note` that names which bound was passed: A names the pane's cap and tells the agent to re-run
with `run_command memory_max`; B names `memory_max=600M`. The last line must be
`PANE-ALIVE: this pane process survived both kills and is still reporting.` — the pane process
that ran the jobs is the same one printing that line, which is the whole point of the change:
the job dies, the pane and its conversation do not.

Pass: all of the above, verbatim in shape (durations and job ids differ). Fail: any exit code
other than -9 for A or B, a missing note, or no PANE-ALIVE line (that means a kill took the pane
with it — the exact regression this card exists to prevent).
