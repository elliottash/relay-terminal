# Package E — the terminal pane's tool-call lines, live

Card: `issues/features/needs_qa_llm/2026-09-18-concise-tool-call-lines.md` (#TK9C), task g1.
Implementer evidence, 2026-09-18. Everything below is a real Relay under Xvfb, driving a real
worker against `pane-stub-provider.py` on 127.0.0.1 — no network, no keys. `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` are all
isolated, so the Relay the owner has running on this machine is untouched (and cannot make the run
look broken through a shared open socket, layout lock or `/tmp/relay-*`).

    docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh          # 00–02
    PHASE=command docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh   # 03
    PHASE=reads   docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh   # 04, 06
    PHASE=diff    docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh   # 05

One click per run: a fold that opens scrolls the view to keep the newest output on screen, so after
the first click the rows are no longer where the script counted them.

The turn the stub drives, in order: a python heredoc, a short command, a failing command, four
consecutive reads, a small edit, a new file, a big edit.

Engine core: **libvterm** (libghostty-vt is not installed on this machine).

| Shot | What it shows |
|---|---|
| `pane-00-bring-your-own-key.png` | the key dialog the first submission opens; the run fills it in and saves |
| `pane-01-agent-ready.png` | configured, the prompt still in the composer |
| `pane-02-lines-folded.png` | **one line per call.** `▸ ran python3 script · 15 lines · exit 0`, `▸ ran ls -la · 9 lines · exit 0` (a short command shown whole), `▸ ran grep -rn 'not-in-this-tree' notes.txt ✗ · exit 1` in the error ink, `▸ read 4 files · 125 lines` (four calls, one row), `▸ edited alpha.py · +2 −1` **with its diff printed underneath**, `▸ wrote brand_new.py · new · 1 line`, `▸ edited big.txt · +30 −30` (too big to print), and the turn's own `✦ 10 tool calls · 1 s` |
| `pane-03-command-unfolded.png` | a click on the first row: the fold opens **in place**, with the muted `command` heading, `$ python3 - <<'PY'` and the heredoc in the command ink, `output`, the fifteen rows, and a last `open in pane` link |
| `pane-04-merged-reads-unfolded.png` | the merged run unfolded: one row per member, `read alpha.py · 2 lines` … each a link to the file it read |
| `pane-05-big-diff-pane.png` | a click on `edited big.txt · +30 −30`: the **diff pane** beside the terminal, `big.txt +30 −30`, red and green on a tint with the old/new gutter |
| `pane-06-member-opened-the-file.png` | a click on a member of the merged fold: that file, in a preview pane |

## What this does not show

- **`agent/show_tool_output` on.** Off is the new behaviour and what the shots cover; on, the
  stream prints under the line as before and the line is final where it stands (the code path is
  `endCallRun()` right after the started row is drawn).
- **The shortcut hint** `call.fold` ("Click a ▸ line to unfold it here · Ctrl+Shift+Return unfolds
  the nearest"). It goes through the shortcut-hint gates, and the run had already spent its global
  gap on the "prompt box is the input" hint (visible in `pane-04`).
- **A ghostty-core run.** libghostty-vt is not installed here. The anchor is written from column 0
  with a `▸ ` placeholder, which is what that core needs to find it.
