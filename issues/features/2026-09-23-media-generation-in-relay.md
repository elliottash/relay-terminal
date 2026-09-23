---
id: 2GV0
type: work
status: discussing
labels: [feature, agent-tools, media]
assignee: codex
waiting_on: owner
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
- Recommendation for video: default to Veo 3.1 Lite, with audio off unless it is asked for (an 8 s 720p clip costs $0.24 silent or $0.40 with sound). Use Wan 3.0 when a clip must be longer than 8 s. Show the estimated cost before each generation, because video is the one medium where a single call costs dollars rather than cents.
