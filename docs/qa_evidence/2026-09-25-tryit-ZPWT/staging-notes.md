# Staging notes, #ZPWT Try it

`stage.sh` is rerunnable and side-effect-free outside these places:
- fixtures + output under `/home/elliott/.cache/relay/scratch/tryit/` (hog.py, pane.py),
- one throwaway transient scope `relay-tryit-pane` in `app-relay.slice` (`--collect`, gone when
  it exits; no unit files, no persistence, no network, no model, no git changes).

What it stages: a pane analog — a python process in that 1500M scope — running two `run_command`
jobs through the landed `ToolExecutor`, exactly the shipped worker path. A: an 1800M hog past the
pane cap with no per-run bound. B: a 900M hog with `memory_max=600M`.

Two defects were found while staging this (both fixed before this bundle landed):
1. Without `MemorySwapMax=0` a job swaps past its bound: a 900M hog survived a 600M `memory.max`.
   `scoped_argv` now pins swap to 0 — a bound a job can sidestep on disk is not a bound. This is
   also covered by a live test (`test_memory_max_kills_only_the_command_when_passed`).
2. The first rehearsal's hog (1500M) landed just under the pane cap with the pane's own overhead;
   the staged hog is 1800M so the pane-cap breach is unambiguous.

Person steps: one — read the run and judge the report. The mechanical steps (staging and this
run) are done; `01-tryit-run.txt` is the capture of the run made while staging.
