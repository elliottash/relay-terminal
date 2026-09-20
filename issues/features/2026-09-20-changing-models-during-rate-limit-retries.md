---
id: DC4J
type: work
status: planning
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# changing models during rate limit retries -- bug

## Issue
there is a bug  i think, where when a model gets a denial eg 429 and starts retrying, i cant switch the model. if i click a new one in the picker, it keeps retrying rather than move immediately. i have to esc to escape and interrupt the retries. that shouldnt happen -- it should just switch over.

too help with this, add /swap as a command that immediately swaps to your default fallback (or back to your first choice provider if you are on the fallback).
