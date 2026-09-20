# Raw measurements, #PF4K startup area

## spark startup matrix (ms from exec)
{"tag": "warm-fresh#0", "pid": 4148124, "load_before": "2.68", "load_after": "2.68", "wall0": 1789922972.8706095, "worker": 208.9, "window": 235.6, "shell": 254.7, "wready": 325.4}
{"tag": "warm-fresh#1", "pid": 4148641, "load_before": "2.68", "load_after": "2.70", "wall0": 1789922975.260724, "worker": 172.7, "window": 199.0, "shell": 217.7, "wready": 242.4}
{"tag": "warm-fresh#2", "pid": 4149102, "load_before": "2.70", "load_after": "2.70", "wall0": 1789922977.5691767, "worker": 224.4, "window": 252.9, "shell": 284.3, "wready": 321.6}
warm-fresh {"window": {"median": 235.6, "min": 199.0, "max": 252.9}, "shell": {"median": 254.7, "min": 217.7, "max": 284.3}, "worker": {"median": 208.9, "min": 172.7, "max": 224.4}, "wready": {"median": 321.6, "min": 242.4, "max": 325.4}, "term_ms": {"median": 63.5, "min": 63.4, "max": 63.8}}
{"tag": "cold-fresh#0", "pid": 4149770, "load_before": "2.70", "load_after": "3.05", "wall0": 1789922980.2764623, "worker": 220.3, "window": 246.5, "shell": 264.5, "wready": 297.3}
{"tag": "cold-fresh#1", "pid": 4150276, "load_before": "3.05", "load_after": "3.05", "wall0": 1789922982.949343, "worker": 217.4, "window": 243.5, "shell": 259.7, "wready": 308.6}
{"tag": "cold-fresh#2", "pid": 4150921, "load_before": "2.88", "load_after": "2.88", "wall0": 1789922985.6445782, "worker": 208.7, "window": 238.5, "shell": 261.5, "wready": 319.5}
cold-fresh {"window": {"median": 243.5, "min": 238.5, "max": 246.5}, "shell": {"median": 261.5, "min": 259.7, "max": 264.5}, "worker": {"median": 217.4, "min": 208.7, "max": 220.3}, "wready": {"median": 308.6, "min": 297.3, "max": 319.5}, "term_ms": {"median": 63.8, "min": 63.7, "max": 113.7}}

## spark idle, 1 pane and 4 panes (60 s samples)
tag one-pane built None panes 1 tabs 1 load 3.50 | total_cpu% 0.53 total_wakeups/s 20.9 nproc 3
  relay                        pid=16746 thr= 45 cpu%=  0.53 wk/s=   20.9 pss=41808 pdirty=30064
  python3 worker.py            pid=16847 thr=  2 cpu%=  0.00 wk/s=    0.0 pss=32044 pdirty=31224
  bash                         pid=16849 thr=  1 cpu%=  0.00 wk/s=    0.0 pss=1071 pdirty=1032
  SUM Pss kB 74923  SUM Private_Dirty kB 62320
tag four-panes built 4 panes 4 tabs 1 load 1.36 | total_cpu% 1.43 total_wakeups/s 59.8 nproc 9
  relay                        pid=45576 thr= 48 cpu%=  1.43 wk/s=   59.8 pss=44162 pdirty=33272
  python3 worker.py            pid=45690 thr=  2 cpu%=  0.00 wk/s=    0.0 pss=32299 pdirty=31624
  bash                         pid=45691 thr=  1 cpu%=  0.00 wk/s=    0.0 pss=1074 pdirty=1036
  python3 worker.py            pid=46698 thr=  2 cpu%=  0.00 wk/s=    0.0 pss=27233 pdirty=26692
  bash                         pid=46699 thr=  1 cpu%=  0.00 wk/s=    0.0 pss=1070 pdirty=1032
  python3 worker.py            pid=47118 thr=  2 cpu%=  0.00 wk/s=    0.0 pss=27273 pdirty=26732
  bash                         pid=47119 thr=  1 cpu%=  0.00 wk/s=    0.0 pss=1066 pdirty=1028
  python3 worker.py            pid=47378 thr=  2 cpu%=  0.00 wk/s=    0.0 pss=26973 pdirty=26432
  bash                         pid=47379 thr=  1 cpu%=  0.00 wk/s=    0.0 pss=1074 pdirty=1036
  SUM Pss kB 162224  SUM Private_Dirty kB 148884

## spark memory across panes/tabs and back
just-started         relay Pss= 40180 kB PrivDirty= 30408 tree Pss= 73219 fds= 25 thr= 49 children= 2 runtime_dirs=2
1-pane-settled       relay Pss= 40179 kB PrivDirty= 30408 tree Pss= 73210 fds= 25 thr= 49 children= 2 runtime_dirs=2
4-panes              relay Pss= 43178 kB PrivDirty= 33380 tree Pss=160829 fds= 46 thr= 52 children= 8 runtime_dirs=5
4-panes+3-tabs       relay Pss= 46432 kB PrivDirty= 36636 tree Pss=220952 fds= 60 thr= 54 children=12 runtime_dirs=7
after-closing        relay Pss= 47362 kB PrivDirty= 36512 tree Pss= 80919 fds= 21 thr= 45 children= 2 runtime_dirs=2
after-closing+20s    relay Pss= 46437 kB PrivDirty= 36512 tree Pss= 79979 fds= 21 thr= 45 children= 2 runtime_dirs=2

## spark open/close churn, 20 cycles
baseline     relay Pss= 40428 kB tree Pss= 73518 fds= 25 thr= 49 children=2 runtime_dirs=2
after-5      relay Pss= 42101 kB tree Pss= 75720 fds= 25 thr= 49 children=2 runtime_dirs=2
after-10     relay Pss= 42275 kB tree Pss= 76165 fds= 21 thr= 45 children=2 runtime_dirs=2
after-15     relay Pss= 42274 kB tree Pss= 76423 fds= 21 thr= 45 children=2 runtime_dirs=2
after-20     relay Pss= 42274 kB tree Pss= 76859 fds= 21 thr= 45 children=2 runtime_dirs=2
settled      relay Pss= 42287 kB tree Pss= 76872 fds= 21 thr= 45 children=2 runtime_dirs=2

## spark restore of a saved 6-pane / 3-tab layout
{
 "step": "restored",
 "built": 7,
 "restored_workers": 6,
 "restored_runtime_dirs": 7,
 "allworkers": 265.3,
 "window": 316.9,
 "relay": {
  "Rss": 152812,
  "Pss": 44697,
  "Private_Dirty": 35324
 },
 "fds": 61,
 "threads": 54,
 "nchild": 12,
 "workers": 6,
 "tree_pss_kb": 211921,
 "tree_pdirty_kb": 199632
}

## spark startup phase timeline (strace -f -tt -T; ~2.5x slower than real, proportions only)

## spark: syscalls/s, one idle pane (bpftrace raw_syscalls, 30 s)
statx 145  ioctl 57  read 44.5  close 37  openat 37  faccessat 36  getdents64 35  newfstatat 27.5  ppoll 24.5  fstat 17.5  writev 4  recvmsg 2

## spark: which paths, one idle pane (bpftrace, 20 s)
Attaching 4 probes...


@gd: 700
@open[/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app]: 20
@open[/proc/87578/cgroup]: 20
@open[/proc/87578/statm]: 51
@open[/proc/87576/statm]: 51
@open[/proc/87576/task/87576/children]: 51
@open[/proc/87578/task]: 51
@open[/proc/87576/stat]: 51
@open[/proc/87578/stat]: 51
@open[/proc/87578/task/87578/children]: 51
@open[/proc/87576/task]: 51
@open[/proc/87576/task/87591/children]: 51
@open[$PROFILE/tmp/relay-DqNlYv/guest-events]: 251
@statx[$PROFILE/cfg/RelayTerminal/relay.conf]: 100
@statx[/etc/xdg/RelayTerminal.conf]: 200
@statx[$PROFILE/cfg/RelayTerminal.conf]: 200
@statx[/home/elliott/.config/kdedefaults/RelayTerminal.conf]: 200
@statx[/etc/xdg/xdg-plasma/RelayTerminal/relay.conf]: 200
@statx[/home/elliott/.config/kdedefaults/RelayTerminal/relay.conf]: 200
@statx[/etc/xdg/RelayTerminal/relay.conf]: 200
@statx[/etc/xdg/xdg-plasma/RelayTerminal.conf]: 200
@statx[$PROFILE/tmp/relay-DqNlYv/guest-events]: 250
@statx[]: 1150

## spark: on-CPU profile, 4 idle panes, innermost Relay frame (perf, 25 s, 243 samples)
 60.9% (no Relay frame: kernel / Qt internals)
 23.9% Pane::pollGuestEvents
  3.7% relay::usage::walkTrees
  2.5% Pane::pollShell
  2.5% relay::usage::metersEnabled
  1.2% Pane::processBusy
  rest <1% each: TerminalView::resolveFoldAnchors, usage::parseStat, readlineReady,
  RelayWindow::refreshPaneStatus, Pane::refreshUsage, isolation::oomKills

## SIGTERM-to-exit, same script both machines (harness/shutdown.py <panes> <tabs> <reps> <tag>)
spark Qt5,      6 panes / 3 tabs, 3 reps: [(7, 264.3), (7, 163.6), (7, 163.8)] median 163.8 ms
sphinxpad Qt5,  6 panes / 3 tabs, 3 reps: [(7, 214.1), (7, 214.3), (7, 164.1)] median 214.1 ms
sphinxpad Qt6,  6 panes: not measured (harness contention on a shared laptop; see FINDINGS.md section 6)
1 pane: spark 63.5 ms, sphinxpad Qt5 63.6 ms, sphinxpad Qt6 63.9 ms
(the pane counter reports 7 because it takes max(worker processes, $TMPDIR/relay-* dirs);
 six panes were built and six workers ran.)
