---
id: 2GV0
type: work
status: discussing
labels: [feature, agent-tools, media]
assignee: codex
waiting_on: owner
rank: m
created: '2026-09-23'
source: 'Owner in a Relay pane, 2026-09-23; card written by Codex'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Give Relay agents image, sound, and video generation

## Issue
i agree, write this in a card. 

can we also bake in sound generation and maybe video generation? 

is gemini the best for that? check what openrouter has at low cost

## Discussion points
- The proposal referred to by “this” is absent from the context passed to this session. Confirm its wording before setting the implementation scope.
- Separate image, speech, music, sound effects, and video capabilities. A provider's support for one does not imply support for the others.
- As checked on 2026-09-23, OpenRouter offers dedicated image, text-to-speech, and asynchronous video APIs, plus Lyria music models through its general audio-output catalog. Discover current capabilities and pricing at runtime rather than hard-coding model choices. Sources: https://openrouter.ai/docs/guides/overview/multimodal/image-generation , https://openrouter.ai/docs/guides/overview/multimodal/tts , https://openrouter.ai/docs/guides/overview/multimodal/video-generation , https://openrouter.ai/collections/audio-models .
- Low-cost examples from the live OpenRouter catalog: FLUX.2 Klein 4B image output starts at $0.014 for the first megapixel; Gemini 3.1 Flash Lite Image is about $0.0336 for a 1K image; Veo 3.1 Lite video is $0.03/second at 720p without audio or $0.05/second with audio; Lyria 3 Clip is $0.04 for 30 seconds and Lyria 3 Pro is $0.08 per song. Speech models have separate per-character pricing. These are price comparisons, not quality rankings. Sources: https://openrouter.ai/black-forest-labs/flux.2-klein-4b/api , https://ai.google.dev/gemini-api/docs/pricing , https://openrouter.ai/api/v1/videos/models , https://openrouter.ai/collections/audio-models .
- Sound effects need a separate capability check. The speech endpoint synthesizes spoken text and Lyria makes music; neither establishes a general sound-effects workflow. Decide whether “sound” means speech, music, effects, or all three.
