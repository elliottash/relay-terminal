# When Relay crashes

Relay died twice on 2026-09-19 (16:30:40 and 16:36:18) and left nothing at all: the log stops
mid-line, there is no core, and nothing on the machine had recorded why. This is what to do about
that, written from working through it. The worked example at the end is that crash — a Markdown
table row with no separator line, which recursed until the 8 MiB stack was gone.

## Was it a crash?

A crash and a quit look the same from the outside: the window is gone.

```
grep -n "gui_start\|gui_quit\|gui_crash" ~/.local/share/relay/logs/relay.log
```

- `gui_quit reason=signal` — it was told to stop (`kill`, a logout, a session end). Not a crash.
- A `gui_start` with **no** `gui_quit` before it, and the lines before it stopping mid-stream —
  that run died without being asked to. That is the shape a crash leaves.
- `gui_crash signal=… name=… addr=… build=…` followed by `gui_crash_frames_begin` — the crash
  report Relay now writes about itself. Read that first; the rest of this file is for when it is
  not enough.

The machine's own crash log says what signal it was, even for a run that predates the handler:

```
sudo grep -B3 "build/relay" /var/log/apport.log | tail -20
```

`called for global pid <pid>, signal 11` is a SIGSEGV. **apport records nothing else about it**:
every entry for our binary ends `executable does not belong to a package, ignoring`, because
`build/relay` is not installed from a `.deb`. `kernel.core_pattern` pipes to apport here, so there
is no core file either, and `ulimit -c` is 0 in an ordinary shell (which a *piped* core pattern
ignores — that is why apport is invoked at all). Do not go looking for `/var/crash/*relay*`: there
will never be one.

Two more things that read as a crash and are not:

- **The window is frozen, not gone.** Under `scripts/relay-debug` a crashed process is *held* by
  gdb while the report is written. It looks hung. Check for it before concluding anything.
- **The binary was rebuilt under a running Relay.** apport then says `executable was modified
  after program start, ignoring` and refuses the entry. The run still died of something; that line
  is about apport's bookkeeping, not about the cause.

## What Relay writes by itself

`src/CrashLog.h` installs handlers for SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT and SIGSYS. A
fatal signal appends to `~/.local/share/relay/logs/relay.log`:

```
…Z ERROR relay.gui gui_crash signal=11 name=SIGSEGV code=1 addr=0x0 pid=545459 uptime_s=12 build=2026-09-19.16H.06
…Z ERROR relay.gui gui_crash_frames_begin count=12
/home/elliott/repos/relay-terminal/build/relay(+0x49b9f0)[0xb35dd862b9f0]
/lib/aarch64-linux-gnu/libQt5Core.so.5(_ZN10QEventLoop4execE…+0x144)[0xf57300c7a614]
…
…Z ERROR relay.gui gui_crash_frames_end · names: addr2line -fCe build/relay <+0x…> · every thread: scripts/relay-debug
```

A frame that is only an offset becomes a name and a line with

```
addr2line -fCe build/relay +0x49b9f0
```

as long as `build/relay` is still the binary that crashed — see "the binary has moved on" below.
It then dies of the same signal, so the exit status is still 139 and a debugger still catches it.

The backend has the same thing: `logs.configure()` turns on `faulthandler`, so a worker that dies
of a signal writes every thread's Python stack to `~/.local/share/relay/logs/worker-faults.log`.
Nothing else would: a fatal signal never reaches the logging module, and the GUI reads the worker's
stderr and throws it away on purpose (no provider error bodies in the log).

Both follow the logging level: "off" means no file, and the report goes to stderr alone.

## When that is not enough: `scripts/relay-debug`

The in-process report gives one thread and no arguments. For the rest, run Relay under gdb:

```
scripts/relay-debug                       # the saved window layout, as the launcher starts it
scripts/relay-debug --workspace ~/src     # extra arguments go to relay
```

It prints where the report goes (`~/.local/share/relay/logs/relay-gdb.<stamp>.log`) and writes it
as it happens, so it can be read from another session while Relay is still up. The report is
bounded on purpose: innermost 40 frames, outermost 40, the depth, locals for the innermost 12, and
25 frames of every other thread.

Reading it:

- **The innermost frames** say where it died. Qt frames at the top (`QArrayData::allocate`,
  `QLayout::parentWidget`) are usually a symptom — keep reading down to the first `relay::` frame.
- **The outermost frames** say who called it, which is the answer whenever the innermost ones are
  a runaway recursion repeating the same frame.
- **The same frame thousands of times** is a stack overflow. Confirm it:
  `grep VmStk /proc/<pid>/status` — 8192 kB (the `ulimit -s` limit) means the stack is full.
- At `-O2` the locals are unreliable (`<optimized out>`, "Corrupted DWARF expression") and gdb's
  unwind of a blown stack can repeat one frame that is not really the recursing one. Believe the
  innermost real frame and the fact of the overflow; do not believe the middle.

**Never edit `scripts/relay-debug` while it is running.** bash reads a script by byte offset, so an
edit under a live run makes the shell resume at a stale offset, execute the gdb command block as
shell commands, relaunch gdb and truncate the report it had just written. That is how the first
crash's full dump was lost.

## Reproduce it outside the app

The fastest way from "a frame in `relay::X`" to a fix is to drive that component on its own. Most
of them are small static libraries (`build/librelay-*.a`) that link against Qt Core alone, so a
throwaway harness in the scratchpad builds in seconds:

```
g++ -g -O1 -fPIC -I/home/elliott/repos/relay-terminal/src fuzz.cpp -o fuzz \
    /home/elliott/repos/relay-terminal/build/librelay-markdown.a $(pkg-config --cflags --libs Qt5Core)
```

Feed it awkward input in a loop with a clock, and fail the run on a timeout as well as on a crash —
a hang and a crash are both bugs and you do not know which one you are chasing yet. Build the
harness at `-O1`: the backtrace is then trustworthy, which the `-O2` app binary's is not, and a
crash there names the real cycle in four frames.

Once it reproduces, cut it down to the smallest input and put that in the component's own test
(`tests/markdownansi_test.cpp` and friends) before fixing anything.

## The binary has moved on

Every session builds the same `build/`, so by the time you read a report the binary may not be the
one that crashed. If the process is still held under gdb, its own copy is still on disk:

```
cp /proc/<pid>/exe /tmp/relay-old                 # even when the file says "(deleted)"
grep -m1 " r-xp " /proc/<pid>/maps                # first mapping = the load base
python3 -c "print(hex(0x<frame addr> - 0x<load base>))"
addr2line -fCie /tmp/relay-old 0x<offset>         # -i also shows inlined frames
```

`build/relay.build-id` (and the `build=` field in every `gui_start` and `gui_crash` line) says
which build a run was: `2026-09-19.16H.03` is the third relink in the 16:00 hour. A running Relay
keeps the binary it started with, so that field, not the file on disk, is what a report is about.

## Worked example — the 2026-09-19 crashes

1. Two runs ended with no `gui_quit`; apport had `signal 11` for the second and refused the first
   because the binary had been rebuilt under it. No core, nothing in the log.
2. Relay was restarted under `scripts/relay-debug`. Twelve minutes later it stopped on SIGSEGV in a
   `QString` allocation under `MarkdownAnsi::decideLine`, with the same frame repeated 27,000 times
   and `VmStk` at exactly 8192 kB — a stack overflow.
3. A fuzz harness over chunked Markdown crashed in seconds outside the app, at `-O1`, where the
   cycle was four frames and plain: `renderTable → renderInline → finish → renderTable`.
4. The minimal input was one line: `| one row and no separator |`. `decideLine` collects any line
   starting with `|` as a table row; `renderTable` finds no `|---|` separator, decides "not a table
   after all", and renders each row through `renderInline`, whose inner renderer collects it as a
   table of its own and calls `renderTable` again. An agent writes such a line by accident often.
5. The fix (63e1ed34) was three lines: a cell's renderer never starts a table, which is simply
   true. The regression test is the minimal input, plus the same streamed a character at a time.

The tooling this file describes — `src/CrashLog.h`, `logs.enable_fault_reports`,
`scripts/relay-debug` — was written during that hunt, in `eebfd4e8`, `18190d67` and `b9cd66a2`.
