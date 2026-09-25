# Mechanical pass — #Z82M Try-it, 2026-09-24

Run by the implementing session against the staged environment (`stage.sh`), on a clean
export of `main` (`ccba73c9` + `69390828`, binary `/tmp/z82m-verify/build/relay`), before
handing the Try-it to the person.

## What ran

The desktop's own media stack — the exact code an agent's `media_generate` call runs —
against the staged gateway, on a **keyless** fresh profile (no keyring, no keys):

- `media.quote` → `provider: relay-free, model: relay-image, estimated_usd: 0.003,
  price_source: "Relay Free gateway, per image"`. No key dialog, no refusal.
- `media.generate` → `lighthouse.png` and (second call) `lighthouse2.png` written into the
  project workspace: real 512×512 PNGs.
- `hosted_quota` events: first image `image_limit: 5, image_used: 1`; second
  `image_used: 2` — the count the chip renders as "· N images left".
- Gateway log (captured in `gateway-stage.log`): one line per image
  (`image install=eba52e2b4328 role=relay-image provider=fake model=fake-image-model
  status=200 total_ms=30`), the prompt nowhere.
- `01-mechanical-pass-app-open.png` — the staged app open on the same workspace/profile.

## What was not driven mechanically

The final GUI link — typing the sentence into the pane and the scripted model's
`media_generate` tool call rendering the picture inline in the conversation — was not
driven: `relay-drive type` reaches Board pages only, not a pane's prompt box. Everything
on either side of that link (the media tool, the gateway route, the quota event, the
chip's data) ran for real above; the inline rendering of a media result is the shipped
`docs/INLINE_MEDIA.md` machinery already serving openrouter images. The person's pass is
the one that closes this link.

## Stage quirks found

- The servers `stage.sh` starts do not survive the terminal session that ran the script
  (they die with its process group). Restart them with the printed pids — or re-run
  `stage.sh` in the terminal the app will be opened from. `stage.sh` itself is
  idempotent; port clashes clear with `fuser -k 4761/tcp 4762/tcp`.
- The desktop caches `/v1/health` for ten minutes, so a desktop that looked while the
  gateway was down needs that long (or a restart) before it sees `relay-image`.
