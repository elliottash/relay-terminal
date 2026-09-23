---
id: 2GV0
type: work
status: planned
labels: [feature, agent-tools, media]
assignee: codex
rank: m
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; card written by Codex
links: {plans: [], commits: [2b17ffb791e7a3da2c43a3521783e1c18e7a565f], evidence: [], related: [], github: null}
---
# Give Relay agents image, sound, and video generation

## Issue
i agree, write this in a card. 

can we also bake in sound generation and maybe video generation? 

is gemini the best for that? check what openrouter has at low cost

## Discussion points
- Owner, 2026-09-23: “proposal A” was a typo. The scope is this card: image, music, sound-effects and video generation.
- Separate image, speech, music, sound effects, and video capabilities. A provider's support for one does not imply support for the others.
- As checked on 2026-09-23, OpenRouter offers dedicated image, text-to-speech, and asynchronous video APIs, plus Lyria music models through its general audio-output catalog. Discover current capabilities and pricing at runtime rather than hard-coding model choices. Sources: https://openrouter.ai/docs/guides/overview/multimodal/image-generation , https://openrouter.ai/docs/guides/overview/multimodal/tts , https://openrouter.ai/docs/guides/overview/multimodal/video-generation , https://openrouter.ai/collections/audio-models .
- Low-cost examples from the live OpenRouter catalog: FLUX.2 Klein 4B image output starts at $0.014 for the first megapixel; Gemini 3.1 Flash Lite Image is about $0.0336 for a 1K image; Veo 3.1 Lite video is $0.03/second at 720p without audio or $0.05/second with audio; Lyria 3 Clip is $0.04 for 30 seconds and Lyria 3 Pro is $0.08 per song. Speech models have separate per-character pricing. These are price comparisons, not quality rankings. Sources: https://openrouter.ai/black-forest-labs/flux.2-klein-4b/api , https://ai.google.dev/gemini-api/docs/pricing , https://openrouter.ai/api/v1/videos/models , https://openrouter.ai/collections/audio-models .
- Sound effects need a separate capability check. The speech endpoint synthesizes spoken text and Lyria makes music; neither establishes a general sound-effects workflow. Decide whether “sound” means speech, music, effects, or all three.
- Owner, 2026-09-23: sound effects should work "ElevenLabs style": a text prompt ("dog barking in the rain") produces one short clip, with an optional duration and a seamless-loop option. ElevenLabs' `eleven_text_to_sound_v2` takes `text`, `duration_seconds` (0.5–30 s), `prompt_influence` (0–1) and `loop`, and returns MP3, PCM or Opus. Source: https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert .
- OpenRouter has no sound-effects model as of 2026-09-23. Its audio-output catalog is Lyria 3 Clip/Pro (music) and gpt-audio/gpt-audio-mini (speech), and its video models with audio (Veo 3.1, Sora 2, Seedance 1.5) generate sound only inside a video. SFX therefore needs a second provider. Low-cost options on fal.ai (one API key, pay per use): ElevenLabs Sound Effects V2 at $0.002/second (≈$0.02 for a 10 s clip), CassetteAI SFX at $0.01 per clip up to 30 s (WAV), MMAudio V2 at $0.001/second (also adds synced audio to an existing video), and Stable Audio 2.5. ElevenLabs' own API bills sound effects in account credits. Sources: https://fal.ai/models/fal-ai/elevenlabs/sound-effects/v2 , https://fal.ai/models/cassetteai/sound-effects-generator , https://fal.ai/models/fal-ai/mmaudio-v2/text-to-audio , https://fal.ai/models/fal-ai/stable-audio-25/text-to-audio .
- **Decided (owner, 2026-09-23):** music uses Lyria (OpenRouter: 3 Clip at $0.04 per 30 s, 3 Pro at $0.08 per song) or ElevenLabs Music (about $0.80 per minute via fal, https://fal.ai/learn/devs/elevenlabs-music-user-guide ). Sound effects use ElevenLabs only. Both ElevenLabs capabilities require a fal.ai key or an ElevenLabs key. Without either, the SFX tool is unavailable and says which key to add, and music falls back to Lyria on the OpenRouter key.
- Recommendation: make the SFX tool's interface the ElevenLabs one (prompt, duration, loop, prompt influence). Back it with fal.ai's ElevenLabs endpoint by default so a single fal key covers it, and allow a direct ElevenLabs key when the user has one. That keeps the model swappable (CassetteAI or MMAudio when cheaper) without changing the tool.
- Video on OpenRouter (live catalog, 2026-09-23; all asynchronous, via one OpenRouter key). Cheapest per second: Veo 3.1 Lite at $0.03 (720p, silent) or $0.05 (720p, with audio), 4/6/8 s clips, first and last frame; Wan 2.6 at $0.04 (480p text-to-video); Wan 3.0 at $0.05 (480p) up to $0.20 (1080p), 2–30 s, audio; Grok Imagine Video at $0.05 (480p) or $0.07 (720p); Hailuo 3 Max at $0.05 (480p, silent); HeyGen Avatar IV at $0.05 for a talking head from one photo. Seedance 1.5 Pro is billed in video tokens; at the usual width × height × fps ÷ 1024 that is roughly $0.01–0.03/s at 480p–720p silent, but that figure is an estimate that has not been checked with a real request. Mid tier: Veo 3.1 Fast $0.08–0.12, Kling v3 Standard $0.084 (+audio $0.126). Top tier: Veo 3.1 $0.20–0.60, Sora 2 Pro $0.30–0.50. Source: https://openrouter.ai/api/v1/videos/models .
- Earlier video recommendation (Veo 3.1 Lite with Wan 3.0 for longer clips) is superseded by the owner's Seedance 2.0 and Veo 3.1 Lite selection below. Show an estimated cost before each generation.

## Decisions
- Owner, 2026-09-23: “i want seedance 2 and veo 3.1 lite”. Offer both OpenRouter video models: standard Seedance 2.0 (`bytedance/seedance-2.0`) and Veo 3.1 Lite (`google/veo-3.1-lite`). The earlier Wan 3.0 fallback recommendation is superseded; do not silently choose Seedance 2.0 Mini or Fast. Source for model identity: https://openrouter.ai/bytedance/seedance-2.0-20260414/api and https://openrouter.ai/google/veo-3.1-lite .

## Done means
- An agent can request an image, a music clip, an ElevenLabs sound effect, or a video from the selected providers and receives a real, reusable file path in the workspace. Image, music and SFX generation use the available OpenRouter, fal.ai or ElevenLabs key as decided; missing keys produce a precise setup message.
- Video offers exactly standard Seedance 2.0 and Veo 3.1 Lite through OpenRouter. A request can be quoted before submission; an asynchronous job can be checked and its finished video downloaded after a worker restart.
- The result records provider, model, settings, estimated and reported cost when available. Invalid settings, stale prices, failed jobs, unsafe URLs and oversized files fail without leaving a misleading success artifact.

## Plan
**Goal.** Give Relay agents four media capabilities—image, music, sound effects and video—using the owner's provider choices, with durable local results and a visible cost estimate before a paid generation. Speech synthesis is a separate capability and is outside this card's agreed scope.

**Findings.** Native agent tools are assembled in `backend/relay_core/agent.py:tools` from `backend/relay_core/tools.py`; guest access is exposed separately by `backend/relay_core/guest_board_bridge.py`. `backend/relay_core/keystore.py` and `src/ModelSettings.cpp` own provider keys; fal.ai and ElevenLabs are service keys, not chat-model presets. `backend/relay_core/attachments.py` and `src/MarkdownAnsi.h` already support local image paths and inline images; `src/FilePanes.cpp` previews images but does not play audio or video. `docs/AGENT-SESSIONS-PROTOCOL.md` specifies worker events and must describe any new media result/status shape.

OpenRouter uses `POST /api/v1/images` (base64 image data) and an asynchronous `POST /api/v1/videos` → poll → content-download flow. The chosen video IDs are `bytedance/seedance-2.0` and `google/veo-3.1-lite`, with different duration, resolution, reference and audio capabilities. Lyria's music response format needs an adapter test against its current OpenRouter endpoint. fal's current ElevenLabs SFX v2 schema supports loop but specifies 0.5–22 seconds; the direct ElevenLabs endpoint allows 0.5–30 seconds. Use `elevenlabs/music/v2.5` on fal or the direct ElevenLabs Music endpoint: fal marks its older `fal-ai/elevenlabs/music` endpoint for deprecation.

**Steps.**
1. Add a media service module under `backend/relay_core/` with small OpenRouter, fal and ElevenLabs adapters. Add service-key lookup/storage for fal and ElevenLabs through `keystore.py`, the worker key messages and the Keys UI, without treating either as a chat provider. Keep credentials in the worker/keyring and redact provider error bodies.
2. Add `media_catalog` and `media_quote` tools. Discover OpenRouter image/video capabilities and pricing with a bounded cache; expose only supported model-specific settings. Quote the selected model, duration/resolution/audio and an estimated cost or an explicit “estimate unavailable,” with the source and timestamp. Do not silently substitute a different provider or model. The quote is shown as a normal tool result before generation.
3. Add `media_generate` plus `media_job` tools with a quote token. Support image via OpenRouter, Lyria through OpenRouter, ElevenLabs Music through fal or direct ElevenLabs, ElevenLabs SFX through fal or direct ElevenLabs, and the two selected video models through OpenRouter. Choose Lyria when neither ElevenLabs key is available; SFX returns the decided key-setup message. Validate each provider's parameter limits, including the different SFX duration caps. Start video asynchronously; persist the job ID and settings so status/download survives worker restart. Report actual usage/cost where the provider supplies it.
4. Save results under a user-chosen workspace path through the existing workspace path guard. Decode or download with MIME and byte-size checks, HTTPS/redirect safeguards and atomic writes; never put binary data or signed provider URLs into chat history. Return path, type, dimensions/duration when available, model and cost metadata. Make the returned path clickable in the existing tool-result/file-opening flow; reuse inline Markdown images for PNGs and open audio/MP4 files with the system viewer until Relay has a native player.
5. Offer the same safe media schemas to guest agents through `guest_board_bridge.py`, routed to the active Relay worker and its stored keys. Document the new tool/result contract in `docs/AGENT-SESSIONS-PROTOCOL.md` and update the agent tool guidance. Keep the paid call explicit and cancelable; a canceled poll does not imply an already submitted job was refunded.

**Risks.** Provider catalog prices and model parameters change; a quote is an estimate, never a guarantee. fal's older music endpoint is being retired. Video and music outputs can be large and delayed; download timeouts, restart recovery and output-size limits matter. Fal SFX duration differs from direct ElevenLabs. Guest tools must not expose keys or permit writes outside the workspace.

**Verify.** Add focused adapter tests with mocked provider responses for image decoding, Lyria audio, fal/direct ElevenLabs, and video submit/poll/download; test model-specific validation, missing-key fallback, stale/unknown quotes, cost reporting, bad URLs, size limits, atomic file failure and restart recovery. Run the relevant `tests/test_tools.py`, `tests/test_keystore.py`, `tests/test_images.py` and new media tests, plus protocol and guest-bridge tests. In an isolated profile, generate one small artifact for each capability with test keys or sandboxed provider responses; confirm image display, audio/video file opening and the pre-generation quote in Relay.
