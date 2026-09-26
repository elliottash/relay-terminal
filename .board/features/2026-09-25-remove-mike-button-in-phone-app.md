---
id: 4Q6B
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Remove Mike button in phone app?

## Issue
Remove Mike button in phone app?

People can use the native mic button in their phone right? Tell me if this will cut off a lot of people from
Voice op. The pc browser app  should keep it I guess.

## Done means
On a phone, no Relay microphone button appears anywhere in the remote web app: not beside the prompt box in the main app (`composer-mic`), not in the Board's reply sheet (`rb-mic`), not in the new-card sheet (`rb-create-mic`), which instead shows the existing "To dictate, use the microphone on your keyboard." note. On a desktop browser — and in the desktop app, which is not touched — the mic button and the clip → desktop-transcription path work exactly as today: the existing voice round-trip tests still pass. Failure is any of: the button still visible on a phone, gone on a desktop browser, or the desktop voice path broken.

## Plan
**Goal.** On a phone, the only microphone is the keyboard's own: every Relay mic button in the remote web app (`app/`) disappears from touch-primary devices, while fine-pointer clients — a PC browser on any page, and the desktop app, which is not touched — keep today's voice clip → desktop transcription unchanged.

**Findings.**
- The owner's question first: this does **not** cut many people off from voice. Phone keyboards dictate into any text field (iOS keyboard mic, Gboard/Samsung voice input) with no setup and no desktop key; Relay's phone mic only added the desktop's own transcriber (OpenRouter key) and kept audio off Apple/Google's servers. The losers are phones whose keyboard has no dictation button — rare, and they can still type.
- `app/app.js` owns the main app's mic: the single `#composer-mic` button (`app/index.html:240`, re-parented into the pane strip at `app/app.js:998-1005`), shown by `updateVoiceUi()` (`app/app.js:1825-1836`): `features.includes('voice') && (capability === 'agent' || 'full')` plus `voiceSupported()` (`app/app.js:1803`). Click → `toggleVoice()` (`app/app.js:1842`).
- `app/board.js` owns the Board's two mics: `replyMic` (`rb-mic`, `app/board.js:336`, painted by `paintReply()` at `:1007-1011`) and the new-card `rb-create-mic` (`app/board.js:1235-1241`), both gated by `voiceUsable()` (`app/board.js:1301`). When that is false the new-card sheet already says "To dictate, use the microphone on your keyboard." (`app/board.js:1240`).
- The phone/desktop split the codebase already uses is the pointer media query `matchMedia('(hover: none) and (pointer: coarse)')` — `app/pane.js:219` calls its match the touch client's signature. Phones and iPads match; PC browsers do not.
- Tests exist and must keep passing: the desktop voice round trip `tests/test_remote_browser.py:514-539` (`composer-mic`), and the Board mic tests `tests/test_board_view.py:1096` (dictates a new card) and `:1137` (hidden → keyboard note). All run headless with a fine pointer, so they keep seeing today's desktop behaviour.

**Steps.**
1. `app/app.js`, voice section: add `const TOUCH_PRIMARY = window.matchMedia('(hover: none) and (pointer: coarse)').matches;` with a comment quoting the owner's decision (2026-09-25: phones use the keyboard's own mic), and extend `updateVoiceUi()`'s `allowed` with `&& !TOUCH_PRIMARY`. The button then never appears on a phone; the `voice` wire path stays for browsers.
2. `app/board.js`: the same `TOUCH_PRIMARY` const beside the voice section, folded into `voiceUsable()` (`app/board.js:1301`), so both `rb-mic` and `rb-create-mic` hide on phones and the existing "To dictate, use the microphone on your keyboard." note appears in the new-card sheet.
3. Tests: in `tests/test_remote_browser.py` add a case that starts a voice-capable session under emulated media (the `Browser` helper's `Emulation.setEmulatedMedia` with features `pointer: coarse`, `hover: none`) and asserts `composer-mic` never un-hides. In `tests/test_board_view.py` add the same emulated-media case asserting `rb-create-mic` is hidden with the keyboard note and `rb-mic` stays hidden on an open card. Leave the existing fine-pointer tests as they are.

**Risks.**
- iPads also match the coarse-pointer query, so they lose the mic too. That follows the owner's own reasoning (the iPad keyboard has the same dictation mic, and the Board already treats an iPad as a big phone — `app/board.css:9`); if the owner wants iPads kept, gate on viewport width instead (`width < 600`, as `app/pane.js:224` does) — one line either way.
- A touch-screen laptop still reports a fine pointer and keeps the button — the safe direction (button present, path intact).
- The gate is read once at load; pointer kind does not change mid-session.

**Verify.** `pytest tests/test_remote_browser.py -k voice` and the Board mic tests (`pytest tests/test_board_view.py -k "microphone"` plus the new emulated-media cases) — old round-trip and new hidden tests all green. Live: on the phone, no mic anywhere and the new-card sheet names the keyboard mic; in a desktop browser, the mic is present and a clip still transcribes.
