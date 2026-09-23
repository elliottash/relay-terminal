---
id: 1MGS
type: work
status: needs-verification
labels: [feature, terminal, agent-ui]
assignee: claude-code
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: m
created: '2026-09-22'
source: Claude Code in a Relay pane, 2026-09-22
links: {plans: [], commits: [607ca070, a920364a, f2dcc610, d239d8dd, 4bdd21c9, 108d92e7, 86c96e93, 74eb6ea3, 35a1ab69, a4cf5b19, d6e7096a, 8e43879b], evidence: [docs/qa_evidence/2026-09-22-inline-images/, docs/qa_evidence/2026-09-22-1MGS-relay-images/], related: [EM1E, MDA7], github: null}
---
# Show images inline: in the terminal and in the agent conversation

## Issue
can relay show me images in line? i think we need that

## Decisions
- 2026-09-22, the owner: "in particular, add to the QA skilling that relay will show you images to
  prove whether a change is working or not". So the agent conversation's inline images are what QA
  proof is shown with. The QA text already says so (the bundled `deliver` skill, `board_tryit_brief.md`,
  and so `issues/POLICY.md`). Until this card lands, those Markdown images appear as their paths.

## Discussion points
Relay cannot show an image anywhere today (checked on main, 2026-09-22):

- **Terminal output.** `docs/ENGINE.md` lists "Sixel / kitty graphics" as ❌ on all three cores. The
  libvterm core, the only one that builds here, has no image support at all; libghostty-vt parses
  kitty graphics but nothing renders them. So `kitten icat`, `imgcat`, `chafa --format=kitty`,
  `timg`, matplotlib's kitty/sixel backends and yazi/ranger previews all show nothing or escape
  garbage.
- **Agent conversation.** Images go *to* the agent (#EM1E: paste, drop, `@path`, "Screenshot this
  pane"), but the transcript shows only the `@path` token and a 🖼 mark: no thumbnail of what you
  attached, and nothing for an image the agent reads, writes or links in Markdown (`![](…)`).

## Done means
1. In a Relay terminal pane, `kitten icat pic.png`-style kitty graphics (direct, file, chunked, zlib, raw RGB/RGBA and PNG), iTerm2 `OSC 1337;File=inline=1` and sixel output each draw the picture inline. It scrolls with its text, is gone after `clear`, and a kitty `a=q` query gets an OK reply, so programs detect support. A malformed, oversized or unreadable image draws nothing and never garbles the text after it.
2. In the agent conversation, a reply with `![alt](/abs/path.png)` (or a path relative to the pane's cwd) shows the picture, and an image attached to a prompt shows as a thumbnail under the prompt. A remote `http(s)` image is not fetched and stays a link.
3. Clicking a picture opens it full size. A saved pane restored after a restart shows its pictures again while their files exist, and shows the alt text or nothing (never escape garbage) when a file is gone.
4. Proven by `relay-engine-tests` (ImageTest, plus the view and session tests that cover it), a MarkdownAnsi test, and screenshots of the real app (isolated profile) under `docs/qa_evidence/2026-09-22-inline-images/`, embedded on the card.

Failure looks like: a blank gap or braille-blank rows where a picture should be, escape text on screen, a picture that stays put while text scrolls, or one that is still there after `clear`.

## Plan
**Goal.** One image layer in the terminal engine that serves both the terminal and the agent conversation, because the conversation is ANSI written into the same grid (`src/MarkdownAnsi.h`, `TerminalSession::writeToDisplay`).

**Findings.** Folds already anchor rows with OSC 8 link runs (`VtCore::hyperlinkRuns`, `kProsePrefix`), and both cores keep those links through scroll, reflow, trimming, clearing and serialization. `TerminalSession::feedWithInputGaps` is the one funnel both PTY output and `writeToDisplay` pass through.

**Steps.**
1. Contract, `engine/core/InlineImage.{h,cpp}`: `relay-image:<row>/<rows>/<cols>/<path>` URIs, placeholder bytes (one U+2800 cell per image row), cell sizing, and a disk cache. Landed as `607ca070`, with `ImageTest` proving it in libvterm.
2. Session, `engine/session/ImageProtocol.*` and the funnel in `TerminalSession.cpp`: parse kitty APC G (a=T/t/p/d/q; t=d/f/t; f=24/32/100; o=z; m=1 chunks; c/r; C=1; q=1/2; replies), iTerm2 OSC 1337 File=, and sixel DCS q. Store the image, then feed `placementBytes`. Parts split across reads work, and size caps apply.
3. View, `engine/view/ImageCache.*` and `TerminalView.*`: a decoded-picture LRU, painting over the image rows (scaled down to the pane, a partly visible image clipped properly), click to open full size, and no treating `relay-image:` as an ordinary hyperlink.
4. Relay, `src/MarkdownAnsi.*` and the prompt-echo path in `src/Pane.h`: `![alt](path)` emits a kitty t=f escape, attachments get thumbnails, and remote URLs are not fetched.
5. Docs (`docs/ENGINE.md` row), one full build, a live drive with screenshots, then needs-verification.

**Risks.** GhosttyCore does not build on this machine. It needs no change, since the interception happens before the core, but it stays uncompiled here. The phone view gets the alt text, not the picture, until its own card. Decoding happens on the GUI thread, so the cache is capped.

**Verify.** `relay-engine-tests` (ImageTest, SessionTest, ViewTest), the MarkdownAnsi test, and screenshots of icat-style output, an agent reply with an image and an attachment thumbnail, embedded on the card.

## Execution Summary
Built as one image layer in the terminal engine, used by both the terminal and the agent conversation.

- **Contract** (`engine/core/InlineImage.*`, 607ca070, d239d8dd): an image is a column of rows, each holding one U+2800 cell with an OSC 8 `relay-image:<row>/<rows>/<cols>/<path>` link. The rows keep their column in the last column and under LNM.
- **Session** (`engine/session/ImageProtocol.*`, 86c96e93, 74eb6ea3): reads kitty graphics (direct/file/temp, chunked, zlib, RGB/RGBA/PNG, put, delete, query with replies, C=1, q), iTerm2 `OSC 1337 File=` and sixel out of the stream before the core sees it, with size caps and resync.
- **View** (`engine/view/ImageCache.*`, `TerminalView.*`, f2dcc610): an LRU of scaled pictures, decoded off-thread when large, painted once per frame, scaled to the pane and clipped. Hover shows the path; a click opens it full size. A missing file shows `[image: name]`.
- **Relay** (`src/MarkdownAnsi.*`, `src/Pane.h`, a920364a, 4bdd21c9): `![alt](path)` in replies (absolute, relative, `~/`, `file://`) draws inline, and remote images are never fetched. Prompt attachments get a 6-row thumbnail. Guest (Claude/Codex) replies take the same path. Images are off in ssh login panes.
- **Restart** (a4cf5b19, d6e7096a): saved pane text keeps image-row links, and the replay filter lets through only links that parse as image rows. The Recently-closed preview strips escapes.
- **Docs:** `docs/ENGINE.md` row (35a1ab69). The ghostty core is 🟡, the same code path, but not built on this machine.

Live, in an isolated profile under Xvfb (`docs/qa_evidence/2026-09-22-inline-images/drive.sh`):

![kitty, iTerm2 and sixel pictures drawn inline, text after them intact](docs/qa_evidence/2026-09-22-inline-images/01-protocols.png)
![attachment thumbnail, reply picture, remote image left as a link](docs/qa_evidence/2026-09-22-inline-images/02-conversation.png)
![after quitting and restarting Relay, both pictures drawn again](docs/qa_evidence/2026-09-22-inline-images/03-restored.png)
![after clear, every picture gone with its text](docs/qa_evidence/2026-09-22-inline-images/04-cleared.png)

The before shots are in the same folder (`before-*.png`).

Left out on purpose: a thumbnail under an agent's tool-call line when it reads an image. The call line is rewritten in place, and Relay's own `read_file` refuses images anyway; see a3's report in the thread. Phone view: gets the alt text, not the picture.

## Tests
- `RELAY_ENGINE_TEST=ImageTest build/engine/relay-engine-tests`: 24/24 (protocols, splits, chunking, zlib, t=f, put, query replies, q=2, C=1, malformed and oversized input, sixel colours, serialize round trip).
- `RELAY_ENGINE_TEST=CoreTest ...`: 52/52, including `imageRowsKeepTheirColumn`.
- `RELAY_ENGINE_TEST=ViewTest ...`: 71/71, including the image-painting cases.
- `RELAY_ENGINE_TEST=SessionTest ...`: 12/12.
- `ctest -R 'markdownansi|wordwrap|calllines|outputlinks|windowstate|closedlist'`: markdownansi 29/29, wordwrap 14/14, calllines 59/59, outputlinks 42/42, windowstate 40/40, and closedlist passing.
- `manual: docs/qa_evidence/2026-09-22-inline-images/` (`drive.sh`, four steps, screenshots above).
