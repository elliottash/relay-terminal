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
