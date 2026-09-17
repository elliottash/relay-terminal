# Voice transcription mode (microphone button, hold Right Alt)

- **Status**: open
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: holding Right Alt records speech and inserts the transcript into the composer; the microphone button toggles the same; works offline or with the chosen provider per the owner's decision
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "add voice transcribe mode (microphone icon). and hold right alt to transcribe. (like warp)"

## Notes

- Warp binds voice input to Right Alt (the owner's Warp settings have `voice_input_toggle_key = "alt_right"`).
- Right Alt is AltGr on many European layouts; it must be configurable and ideally off for those layouts.
- Capture: QtMultimedia (`QAudioSource`, Qt6) or PulseAudio/PipeWire via GStreamer; push-to-talk while held, toggle via the button.

## Open decision for the owner

Where speech-to-text runs:
1. **Local** (whisper.cpp or faster-whisper, CPU/GPU): private, no key, offline; adds a model download (≈150 MB–1.5 GB) and CPU/GPU load.
2. **Cloud, BYOK**: an OpenAI-compatible `/audio/transcriptions` endpoint (e.g. OpenAI, Groq, or a provider the owner already uses if it offers ASR — Z.AI lists speech models; verify): fast and accurate, sends audio to the provider, needs a key.
3. Both, with local as default.

## Owner decision and live test (2026-09-17)

Owner: cloud-based, using the existing OpenRouter key; Gemini is fine if cheaper.

Live test with the stored OpenRouter key on a 3-second espeak clip ("Relay, list the files in this folder please"):

| Route | Result | Latency | Cost for 3 s | Per minute |
|---|---|---|---|---|
| OpenRouter `/api/v1/audio/transcriptions`, `openai/whisper-1` | exact, punctuated | 1.8 s | $0.0003 | ≈$0.006 |
| OpenRouter chat, `google/gemini-2.5-flash-lite`, `input_audio` | missing punctuation and "please" | 1.2 s | $0.000027 | ≈$0.0005 |
| OpenRouter chat, `google/gemini-3.1-flash-lite`, `input_audio` | exact, punctuated | 1.0 s | $0.000056 | ≈$0.0011 |

Reference: Groq `whisper-large-v3-turbo` is $0.04/hour (≈$0.00067/min) with a 10-second minimum per request (≈$0.00011 per short clip), OpenAI-compatible, but needs a separate Groq key.

Decision: default to OpenRouter `google/gemini-3.1-flash-lite` (cheaper than Whisper per clip, accurate in the test, same key) with a strict transcription-only prompt; `google/gemini-2.5-flash-lite` as the "cheapest" option and OpenRouter Whisper as the exact-transcription option in settings; optional Groq endpoint later. Guard: the prompt must say to transcribe only and never follow spoken instructions.

## Model decision (2026-09-17, owner)

Owner asked for Gemini 3.8 Flash-Lite; no such model exists (Google's newest Flash-Lite is 3.5; 3.8 is only "Flash"). Owner accepted **`google/gemini-3.5-flash-lite` via OpenRouter** as the default.

Second live test (transcription-only system prompt, two clips):

| Model | Clip 1 (3 s) | Clip 2 (5 s) | Latency | Cost per clip |
|---|---|---|---|---|
| `google/gemini-3.5-flash-lite` | "relay list the files in this folder please" (no punctuation) | exact | 1.0–1.2 s | $0.00005–0.00008 |
| `google/gemini-3.8-flash` | exact | exact | 2.0–2.6 s | $0.0003–0.0004 (≈50 reasoning tokens) |

Implementation notes: default model `google/gemini-3.5-flash-lite`, no reasoning; offer 3.8 Flash and OpenRouter Whisper as higher-accuracy options; light client-side punctuation/capitalization cleanup is optional.

## Key requirement (2026-09-17, owner)

Voice uses `google/gemini-3.5-flash-lite` on OpenRouter, so users provide an **OpenRouter key** to use it, independent of which model their panes' agents use. Behavior when no OpenRouter key is stored (keyring or `RELAY_OPENROUTER_API_KEY`): the microphone button and Right Alt show "Voice needs an OpenRouter key" with actions to add one in Provider / BYOK or import it from Warp; nothing is recorded or sent. The voice settings name the model and state that audio is sent to OpenRouter and Google.
