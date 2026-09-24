---
name: analysis-run
description: Run a data-analysis pipeline end to end, check its headline numbers against the recorded metric, and show the figures it produced. "rerun the analysis", "reproduce table 2".
short: 'Run an analysis pipeline; headline metric checked against the record, figures looked at.'
profile: |
  artifact: number
  primary: metric
  also: ai-visual, script
  human: optional
  criteria: the headline figure and the main table match the paper's claim to the stated precision
  sample: 1/10 after 30
  sign_off: none
  effort: medium
  stakes: rework
  blast: capability
  regularity: routine
  executable: yes
  rot: medium
  rot_reason: data sources and package versions move under the pipeline
  confidential: no
  money: no
---

# Analysis run

A fault in the pipeline is a fault in every run, so the pipeline is versioned and a change to
it is a separate card from a run of it.

1. Record the inputs: data snapshot, pipeline commit, package versions, host.
2. Run the pipeline from clean, capturing stdout and stderr to the run's own folder.
3. Compare every headline number with the recorded value and its tolerance; a miss stops the
   run and is reported with both numbers.
4. Look at each figure the run produced: axes labelled, ranges sensible, no empty panels.
5. Run the pipeline's own tests if it has them.
6. Write the run record: inputs, outputs, the metric comparison, wall time.
7. A person reads a sample of the runs; when the metric drifts, every run until it is
   explained.
