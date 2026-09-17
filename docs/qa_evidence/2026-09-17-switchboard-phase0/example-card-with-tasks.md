---
id: K7Q2
type: work
status: in-progress
labels: [voice, mvp]
component: [gui, worker]
milestone: desktop-alpha
assignee: agent
implemented_by: Claude Opus 5 (pane 2)
rank: 0i
created: '2026-09-17'
acceptance: holding Right Alt records speech and inserts the transcript
source: 'issues/feature_intake.txt, 2026-09-17: "add voice transcribe mode"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Voice transcription mode (microphone button, hold Right Alt)

## Request
add voice transcribe mode (microphone icon). and hold right alt to transcribe.

## Tasks
- [x] Add OpenRouter transcription call <!-- t:z6 -->
- [ ] Record audio with QAudioSource <!-- t:p8 s=in-progress -->
  - [ ] Handle device permission errors <!-- t:q8 blocked_by=p8 -->
- [ ] #M3XJ Right-Alt push-to-talk <!-- t:sk card=M3XJ -->
- [x] ~~Local whisper.cpp fallback~~ <!-- t:0d s=dropped -->

