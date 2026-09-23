<!-- relay:entry 20260922T120000Z-a1 author=claude-code kind=question -->
### Claude Code · 2026-09-22 12:00
Filed from the owner's message. Relay shows no images today, in the terminal or in the agent
conversation (see Discussion points). Questions:

1. **Which comes first?** Recommendation: the agent conversation. It is entirely ours (Qt), needs no
   terminal protocol, and covers the everyday case: thumbnails of attached images and screenshots,
   images the agent reads or writes, and Markdown `![](path)` in replies. Click to open full size.
2. **Terminal protocol.** Recommendation: kitty graphics first (what Ghostty, WezTerm and Kitty
   converge on, and what `icat`, `chafa` and yazi prefer), then iTerm2's OSC 1337 `File=` (small;
   `imgcat`), then sixel last. Images anchored to cells so they scroll and are cleared with the text.
   On libvterm this needs our own APC/OSC handler plus a painted image layer in the view.
3. **Over SSH/mosh.** kitty and OSC 1337 pass through SSH untouched; mosh strips them. Accept that for
   now (it is mosh's limit, not ours)? Recommendation: yes, and say so when an image is dropped.
