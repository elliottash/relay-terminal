# Step-0 gate probes, 2026-09-25 (systemd 255, cgroup v2, 122 GiB RAM)

Throwaway units; probe slice `app-relay-probe.slice` reverted after each run.

1. Slice nesting + binding: `systemctl --user set-property --runtime
   app-relay-probe.slice MemoryMax=1500M MemorySwapMax=0` created the slice (runtime drop-ins
   under `/run/user/1000/systemd/user.control/`, nothing persistent; `revert` removed them).
   A scope started `--slice=app-relay-probe.slice` with no own MemoryMax landed at
   `.../app.slice/app-relay.slice/app-relay-probe.slice/<scope>.scope` and hogs totalling past
   1500M were killed by the SLICE's limit.
2. Single-process kill: scope with `-p OOMPolicy=continue`, staggered hogs (850M at
   oom_score_adj 500 resident first, then 850M at 1000):
   `RESULT adj500_exit=0 adj1000_exit=137 oom_kill=1` — exactly one process died, parent and
   500-adj sibling survived, scope finished normally. (A simultaneous-growth run killed both:
   the second kill raced the first victim's reclaim — noted, the staggered case is the real one.)
3. oom_score_adj precedence: the 1000-adj hog was the kernel's pick over the 500-adj one.
4. Own limit inside the slice: scope with `-p MemoryMax=300M` inside the 1500M slice ran a
   250M hog fine — a child scope's own cap coexists with slice membership (run_command
   memory_max design).
5. `systemd-run --user --scope` works from agent-worker children with
   DBUS_SESSION_BUS_ADDRESS unset (sd-bus falls back to $XDG_RUNTIME_DIR/bus); own cgroup
   observed: relay-pane-<token>-agent-1.scope.
