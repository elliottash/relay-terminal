# Real authenticated guest follow-up

**Historical pre-fix evidence. Resolved by d7c27b95; see [rank1-fixed](../rank1-fixed/README.md). Item 1 now passes.**

Live shared `build/relay`, isolated Xvfb/HOME/config/data/runtime; installed real Codex, existing signed-in auth copied only into disposable HOME and removed on exit. No credentials copied here. No agent was delegated. Prompt: `Reply with exactly MDL1_OK. Do not use tools or read files.`

`before-first-prompt.png` shows gpt-5.6-sol/low. `worker-sanitized.log` shows no configure before the prompt, then guest_harness_started guest=codex, configured host=codex model=gpt-5.6-sol, one successful turn in 4219 ms, tools=0. `after-15s.png` shows MDL1_OK. The real authenticated guest startup/turn works without additional owner setup.

However, the seeded and persisted main priority is `guest:codex|gpt-6-astra|low`, while the displayed and executed model is gpt-5.6-sol. This run therefore does not pass the whole rank-1 guest requirement. Catalog discovery/fallback versus a startup selection defect is not resolved here. Item 1 remains partial; live phone item 11 remains blocked.

An earlier isolated attempt, without adding the installed CLI directory to the shell PATH, used Relay Free and also returned MDL1_OK. That was a hosted generation call and is not guest evidence. The successful retry explicitly exposes the installed CLI and disables hosted fallback through the disposable Python shim retained in the driver. Two minimal generation calls total; neither used tools. All earlier 19-item UI/configuration runs made no generation calls.

This is a follow-up on the concurrently updated shared binary, not a new exact-tree code test. The original shortcut fix has its separate exact-tree build evidence. The script retains temporary paths and waits for a `go` marker after the pre-prompt screenshot; credentials are removed in finally. No raw authentication status or credential files are included.
