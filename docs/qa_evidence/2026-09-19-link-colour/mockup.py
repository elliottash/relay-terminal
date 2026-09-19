#!/usr/bin/env python3
"""Green Means Open: a before/after mockup of one Relay pane per shipped theme.

Every colour on the page is read from data/theme/themes/<id>.toml, so the page shows what the
app paints, not a guess. "Before" is the app as of 2026-09-18: the engine's private link blue
(#6cb6ff) under the pointer only, a per-theme teal for the composer's path token, ANSI 4 for a
Markdown link, ANSI 3 (amber) for inline code, the accent for a URL in the chrome and for the file
preview's host chip. "After" is `[ui] link` — the owner's "dark green, like Warp" — everywhere a
thing can be opened (docs/ARCHITECTURE.md § 14, "Green means you can open it").

    python3 mockup.py [repo root] > green-means-open.html
"""
import html
import math
import sys
import tomllib
from pathlib import Path

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3]
THEMES = ROOT / "data/theme/themes"
ORDER = ["dark-copper", "ibm-beige", "relay-dark", "relay-light", "gruvbox-dark"]
OLD_PATH = {"relay-dark": "#66d0c0", "dark-copper": "#66d0c0", "gruvbox-dark": "#8ec07c",
            "relay-light": "#0f6f68", "ibm-beige": "#0f5f5a"}
OLD_ENGINE_LINK = "#6cb6ff"


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def hexa(c):
    return "#%02x%02x%02x" % tuple(int(round(v)) for v in c)


def mix(a, b, wa):
    return tuple(a[i] * wa + b[i] * (1 - wa) for i in range(3))


def lin(v):
    s = v / 255
    return s / 12.92 if s <= 0.03928 else ((s + 0.055) / 1.055) ** 2.4


def lum(c):
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])


def ratio(a, b):
    la, lb = lum(a), lum(b)
    la, lb = max(la, lb), min(la, lb)
    return (la + .05) / (lb + .05)


def lab(c):
    r, g, b = [lin(v) for v in c]
    x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047
    y = r * 0.2126 + g * 0.7152 + b * 0.0722
    z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return 116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))


def de(a, b):
    return math.dist(lab(a), lab(b))


def alpha(c, a):
    return "rgba(%d,%d,%d,%.3f)" % (c[0], c[1], c[2], a / 255)


class Theme:
    def __init__(self, tid):
        d = tomllib.loads((THEMES / f"{tid}.toml").read_text(encoding="utf-8"))
        self.id = tid
        self.name = d["theme"]["name"]
        self.light = d["theme"].get("variant") == "light"
        ui, syn, term = d["ui"], d["syntax"], d["terminal"]
        self.ui = {k: rgb(v) for k, v in ui.items()}
        self.syn = {k: rgb(v) for k, v in syn.items()}
        self.term_bg = rgb(term["background"])
        self.term_end = rgb(term["background_end"]) if "background_end" in term else self.term_bg
        self.term_fg = rgb(term["foreground"])
        self.ansi = [rgb(v) for v in term["palette"]]
        self.square = d.get("flags", {}).get("square", False)

    def u(self, k):
        return self.ui[k]


def esc(s):
    return html.escape(s, quote=False)


def span(text, color=None, bold=False, underline=None, bg=None, dim=False):
    st = []
    if color:
        st.append("color:%s" % hexa(color))
    if bold:
        st.append("font-weight:700")
    if underline:
        st.append("text-decoration:underline;text-decoration-color:%s;text-underline-offset:3px" % hexa(underline))
    if bg:
        st.append("background:%s" % hexa(bg))
    if dim:
        st.append("opacity:.7")
    return '<span style="%s">%s</span>' % (";".join(st), esc(text))


def pane(t: Theme, after: bool):
    """One terminal pane, mid-session: a build that fails, an agent that finds why, a fix, a push."""
    ui = t.u
    link = ui("link") if after else rgb(OLD_ENGINE_LINK)
    path_tok = ui("link") if after else rgb(OLD_PATH[t.id])
    md_link = t.ansi[2] if after else t.ansi[4]
    plain = t.ansi[15]          # the agent's prose: bright white (97)
    fg = t.term_fg
    muted = ui("text_muted")
    text = ui("text")
    err = ui("error")
    bg = ui("background")
    remote_fill = mix(err, bg, 0.26)
    remote_line = mix(err, bg, 0.8)
    band_fill = mix(err, bg, 0.13 if t.light else 0.18)
    hatch = hexa(mix(err, bg, 0.17 if t.light else 0.24))
    raised = ui("surface_raised")
    radius = "0" if t.square else "8px"
    chip_r = "0" if t.square else "5px"
    prompt = span("elliott@sphinxpad", ui("shell")) + span(" ~/repos/relay-terminal ", fg) + span("main ", t.ansi[5]) + span("❯ ", ui("shell"))

    def cmd(s):            # what the user typed: the shell's colour (Ink::User)
        return prompt + span(s, ui("shell"))

    def out_path(s):       # a path or URL in program output: plain before, the link colour after
        return span(s, link if after else fg)

    def prose_path(s):     # the same, in the agent's bright-white prose
        return span(s, link if after else plain)

    def tool_path(s):      # the same, in a muted tool line
        return span(s, link if after else muted)

    def hovered(s):        # under the pointer: the underline, engine blue before, the link after
        return span(s, link if after else fg, underline=link)

    def code(s):           # `inline code` in prose: amber before, bold after (and a path takes the link)
        return span("`", plain) + (span(s, link, bold=True) if after else span(s, t.ansi[3])) + span("`", plain)

    r = []
    r.append(cmd("ls"))
    r.append("  ".join([span("build", t.ansi[12], bold=True), out_path("CMakeLists.txt"), span("data", t.ansi[12], bold=True),
                        span("docs", t.ansi[12], bold=True), span("engine", t.ansi[12], bold=True), out_path("README.md"),
                        span("scripts", t.ansi[12], bold=True), span("src", t.ansi[12], bold=True), span("tests", t.ansi[12], bold=True),
                        span("WARP.md", link if after else fg)]))
    r.append(cmd("cmake --build build -j18 2>&1 | tail -4"))
    r.append(out_path("/home/elliott/repos/relay-terminal/src/Pane.h:8642:5") + span(": ", fg)
             + span("error: ", t.ansi[9], bold=True) + span("expected ‘;’ before ‘}’ token", fg))
    r.append(span(" 8642 |     }", fg))
    r.append(span("      |     ", fg) + span("^", t.ansi[10], bold=True))
    r.append(span("make: *** [", fg) + out_path("Makefile:91") + span(": all] Error 2", fg))
    r.append(cmd("git status --short"))
    r.append(span(" M ", t.ansi[1]) + span("src/Pane.h", t.ansi[1]))
    r.append(span(" M ", t.ansi[1]) + span("src/Theme.cpp", t.ansi[1]))
    r.append(span("?? ", t.ansi[1]) + span("docs/notes.md", t.ansi[1]))
    r.append(span("* ", ui("agent")) + span("why does the build fail?", ui("agent")))
    r.append(span("  ⚙ read ", muted) + tool_path("src/Pane.h:8630-8650"))
    r.append(span("  ⚙ ran ", muted) + span("git diff -U0 src/Pane.h", muted))
    r.append(span("Line 8641 of ", plain) + code("src/Pane.h") + span(" closes the lambda without its ", plain)
             + code(";") + span(". The same slip is on the", plain))
    r.append(span("branch already — see ", plain) + span("the PR", md_link, underline=md_link)
             + span(" and card ", plain) + prose_path("#K7Q2") + span(". Fix and rebuild?", plain))
    r.append(span("✦ 2 tool calls", muted) + span("  ·  ", muted) + span("open in pane", link, underline=link)
             + span("  ·  ", muted) + span("open Pane.h", link, underline=link))
    r.append(span("* ", ui("agent")) + span("yes", ui("agent")))
    r.append(span("  ⚙ edit ", muted) + tool_path("src/Pane.h") + span("  +1 −1", muted))
    r.append(span("  ⚙ ran ", muted) + span("cmake --build build -j18", muted) + span("  ✓ 0 errors", ui("success")))
    r.append(span("Done: ", ui("success"), bold=True) + span("built clean. ", plain) + span("Docs: ", plain)
             + prose_path("https://relay-terminal.ai/docs/themes") + span(".", plain))
    r.append(cmd("git push origin main"))
    r.append(span("remote: Create a pull request for 'main' on GitHub by visiting:", fg))
    r.append(span("remote:      ", fg) + hovered("https://github.com/relay-terminal/relay/pull/new/main"))
    r.append(span("To github.com:relay-terminal/relay.git", fg))
    r.append(span("   ab8f37e..c1d2e3f  main -> main", fg))
    r.append(prompt + '<span class="cursor" style="background:%s"></span>' % hexa(fg))
    grid = "\n".join('<div class="row">%s</div>' % x for x in r)

    comp = (span("sed", t.syn["command"]) + " " + span("-n", t.syn["flag"]) + " " + span("'8630,8650p'", t.syn["string"])
            + " " + span("src/Pane.h", path_tok) + " " + span("|", t.syn["operator"]) + " " + span("less", t.syn["command"]))
    chip = '<span class="chip" style="color:%s;border-color:%s;border-radius:%s">TERMINAL</span>' % (
        hexa(ui("shell")), alpha(ui("shell"), 150), chip_r)
    composer = ('<div class="composer" style="background:%s;border:1px solid %s;border-radius:%s;color:%s">%s'
                '<span class="mono">%s</span></div>' % (hexa(raised), alpha(ui("accent"), 150), radius, hexa(text), chip, comp))

    header = ('<div class="hdr" style="background:%s;background-image:repeating-linear-gradient(135deg,%s 0 3px,transparent 3px 11px);'
              'border-bottom:1.5px solid %s;color:%s">'
              '<span class="glyph" style="color:%s">●</span><span class="title">zsh — relay-terminal</span>'
              '<span class="rchip" style="background:%s;border:1px solid %s;color:%s;border-radius:%s">⇄ elliott@sphinxpad</span></div>'
              % (hexa(band_fill), hatch, hexa(remote_line), hexa(text), hexa(ui("shell")), hexa(remote_fill),
                 hexa(remote_line), hexa(text), chip_r))

    if after:
        host = '<span class="rchip" style="background:%s;border:1px solid %s;color:%s;border-radius:%s">⇄ sphinxpad</span>' % (
            hexa(remote_fill), hexa(remote_line), hexa(text), chip_r)
    else:
        host = '<span class="rchip" style="background:%s;border:1px solid %s;color:%s;border-radius:%s;font-weight:700">sphinxpad</span>' % (
            alpha(ui("accent"), 40), alpha(ui("accent"), 150), hexa(ui("accent")), chip_r)
    preview = ('<div class="preview" style="background:%s;color:%s;border-top:1px solid %s">'
               '<span class="sub">file preview</span><span class="title">Pane.h</span>%s<span class="btns" style="color:%s">⟳  ↗  Save</span></div>'
               % (hexa(bg), hexa(text), hexa(ui("border")), host, hexa(muted)))
    url_color = ui("link") if after else ui("accent")
    sessions = ('<div class="sessions" style="background:%s;color:%s;border-top:1px solid %s">'
                '<span class="sub">sessions</span><span style="color:%s">share link</span> <span style="color:%s">https://relay-terminal.ai/j/7f3k…</span></div>'
                % (hexa(bg), hexa(text), hexa(ui("border")), hexa(muted), hexa(url_color)))
    grid_style = "background:linear-gradient(%s,%s);color:%s" % (hexa(t.term_bg), hexa(t.term_end), hexa(fg))
    return ('<div class="pane" style="background:%s;border:1px solid %s;border-radius:%s">%s'
            '<div class="grid mono" style="%s">%s</div>%s%s%s</div>'
            % (hexa(bg), hexa(ui("border_strong")), radius, header, grid_style, grid, composer, preview, sessions))


def swatch(c, label, sub=""):
    return ('<div class="sw"><span class="dot" style="background:%s"></span><span class="lab">%s</span>'
            '<span class="sub mono">%s %s</span></div>' % (hexa(c), esc(label), hexa(c), esc(sub)))


def numbers(t: Theme):
    link = t.u("link")
    grounds = [("terminal", t.term_bg), ("raised face", t.u("surface_raised")), ("window", t.u("background"))]
    if t.term_end != t.term_bg:
        grounds.insert(1, ("terminal, foot", t.term_end))
    worst = min(grounds, key=lambda g: ratio(link, g[1]))
    cells = ['<td class="mono">%s</td>' % hexa(link),
             '<td>%.2f:1 <span class="sub">on the %s</span></td>' % (ratio(link, worst[1]), worst[0])]
    for k in ("success", "shell", "agent"):
        cells.append('<td>%.0f</td>' % de(link, t.u(k)))
    cells.append('<td>%.0f</td>' % de(link, t.ansi[2]))
    return "".join(cells)


def legend(t: Theme):
    items = [swatch(t.u("link"), "link", "you can open this"), swatch(t.u("success"), "success", "finished"),
             swatch(t.ansi[2], "ANSI 2", "a program's green"), swatch(t.u("shell"), "shell", "the terminal"),
             swatch(t.u("agent"), "agent", "the agent"), swatch(t.u("error"), "error", "failed · another machine")]
    return '<div class="legend">%s</div>' % "".join(items)


def section(t: Theme):
    return f'''
<section class="theme" id="{t.id}">
  <header class="theme-head">
    <h2>{esc(t.name)}</h2>
    {legend(t)}
  </header>
  <div class="pair">
    <figure><figcaption>Before</figcaption>{pane(t, False)}</figure>
    <figure><figcaption>After</figcaption>{pane(t, True)}</figure>
  </div>
</section>'''


def page(themes):
    rows = "".join('<tr><th scope="row"><a href="#%s">%s</a></th>%s</tr>' % (t.id, esc(t.name), numbers(t)) for t in themes)
    sections = "".join(section(t) for t in themes)
    return f'''<title>Green Means Open</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600;700&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
:root {{ --bg:#f5f3ee; --ink:#1d1b17; --muted:#6a655c; --rule:#d9d4ca; --accent:#1a5548; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  --bg:#141518; --ink:#e8e4de; --muted:#9a948b; --rule:#2c2e34; --accent:#12a457; }} }}
:root[data-theme="dark"] {{ --bg:#141518; --ink:#e8e4de; --muted:#9a948b; --rule:#2c2e34; --accent:#12a457; }}
body {{ background:var(--bg); color:var(--ink); font-family:"IBM Plex Sans",system-ui,sans-serif; font-size:15px; line-height:1.5;
  padding-inline:20px; padding-block:28px 60px; max-width:1240px; margin:0 auto; }}
.mono {{ font-family:"IBM Plex Mono",ui-monospace,Menlo,Consolas,monospace; }}
h1 {{ font-size:2rem; font-weight:600; letter-spacing:-.01em; margin:0 0 .3rem; text-wrap:balance; }}
h2 {{ font-size:1.3rem; font-weight:600; margin:0; }}
p {{ max-width:68ch; }}
.lede {{ color:var(--muted); margin:0 0 1.6rem; }}
.intro {{ display:grid; gap:1rem; margin-bottom:2.2rem; }}
table.nums {{ border-collapse:collapse; font-size:.9rem; width:100%; max-width:820px; }}
table.nums th, table.nums td {{ text-align:left; padding:.35rem .6rem; border-bottom:1px solid var(--rule); vertical-align:top; }}
table.nums thead th {{ font-weight:500; color:var(--muted); font-size:.78rem; text-transform:uppercase; letter-spacing:.06em; }}
table.nums th[scope=row] a {{ color:var(--ink); text-decoration:none; font-weight:500; }}
table.nums td {{ font-variant-numeric:tabular-nums; }}
.sub {{ color:var(--muted); font-size:.85em; }}
.wrap {{ overflow-x:auto; }}
.changes {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(240px,1fr)); gap:.6rem 1.4rem; margin:0; padding:0; list-style:none; font-size:.92rem; }}
.changes li {{ padding-left:.9rem; border-left:2px solid var(--rule); }}
.changes b {{ font-weight:600; }}
section.theme {{ margin-top:2.6rem; padding-top:1.4rem; border-top:1px solid var(--rule); }}
.theme-head {{ display:flex; flex-wrap:wrap; align-items:baseline; gap:.6rem 1.6rem; margin-bottom:1rem; }}
.legend {{ display:flex; flex-wrap:wrap; gap:.3rem 1.1rem; font-size:.82rem; }}
.sw {{ display:inline-flex; align-items:center; gap:.4rem; }}
.sw .dot {{ width:12px; height:12px; border-radius:50%; display:inline-block; box-shadow:0 0 0 1px rgba(128,128,128,.35); }}
.sw .lab {{ font-weight:500; }}
.sw .sub {{ font-size:.78rem; }}
.pair {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(380px,1fr)); gap:1.2rem; }}
figure {{ margin:0; min-width:0; }}
figcaption {{ font-size:.78rem; text-transform:uppercase; letter-spacing:.08em; color:var(--muted); margin-bottom:.4rem; font-weight:500; }}
.pane {{ font-size:12px; line-height:1.45; overflow:hidden; box-shadow:0 1px 2px rgba(0,0,0,.08); }}
.hdr {{ display:flex; align-items:center; gap:.5rem; padding:5px 10px; font-size:12px; }}
.hdr .glyph {{ font-size:9px; }}
.hdr .title {{ font-weight:600; flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }}
.rchip {{ font-size:11px; padding:0 6px; line-height:17px; font-weight:600; white-space:nowrap; }}
.grid {{ padding:8px 12px 6px; overflow-x:auto; }}
.row {{ white-space:pre; line-height:1.5; }}
.cursor {{ display:inline-block; width:7px; height:13px; vertical-align:-2px; }}
.composer {{ margin:8px 10px; padding:6px 10px; display:flex; align-items:center; gap:.6rem; font-size:12px; }}
.chip {{ font-size:9.5px; letter-spacing:.06em; font-weight:700; padding:0 6px; line-height:16px; border:1px solid; }}
.preview, .sessions {{ display:flex; align-items:center; gap:.6rem; padding:6px 12px; font-size:12px; }}
.preview .title {{ font-weight:600; }}
.preview .btns {{ margin-left:auto; letter-spacing:.05em; }}
.note {{ font-size:.9rem; color:var(--muted); max-width:68ch; }}
@media (max-width:480px) {{ .pair {{ grid-template-columns:1fr; }} body {{ padding-inline:16px; }} }}
</style>

<h1>Green means you can open it</h1>
<p class="lede">One Relay pane per theme, mid-session, before and after the <span class="mono">link</span> token. Every colour is read from the theme file the app ships.</p>

<div class="intro">
<ul class="changes">
  <li><b>Program output.</b> A path, URL or <span class="mono">#card</span> that resolves is green at rest, not only under the pointer. A colour the program chose (<span class="mono">git status</span> red, <span class="mono">ls</span> blue) stays. Option, on by default.</li>
  <li><b>Hover and fold rows.</b> The underline and “open in pane · open Pane.h” were a fixed <span class="mono">#6cb6ff</span> in every theme, 1.2:1 on beige. Now the theme’s link.</li>
  <li><b>Agent prose and tool lines.</b> A Markdown link moves from ANSI 4 to ANSI 2. Inline code is bold instead of amber, so a filename in backticks can take the link colour and amber keeps its one job. A path in a tool line takes it too.</li>
  <li><b>Composer.</b> The path token was a teal that appeared nowhere else. It is the link colour: a path you type is one you can open.</li>
  <li><b>Chrome.</b> A URL on the Sessions page was the accent (copper, navy). Now the link.</li>
  <li><b>Hostname.</b> The file preview’s host chip was the accent; the pane header’s was red and hatched. They are one chip now: “not this machine” looks the same everywhere.</li>
</ul>

<div class="wrap"><table class="nums">
<thead><tr><th>Theme</th><th>link</th><th>Worst contrast</th><th>ΔE vs success</th><th>vs shell</th><th>vs agent</th><th>vs ANSI 2</th></tr></thead>
<tbody>{rows}</tbody>
</table></div>
<p class="note">“Dark green” is as dark as the contrast contract allows: 4.5:1 on every ground a link is read on, which on a charcoal theme lands a mid green rather than a forest one. ΔE is CIELAB 1976; 20 is the bar the theme tests hold two meaning colours to, and here it keeps “you can open it” apart from “it finished” and from a program’s own green. <span class="mono">tests/theme_test.cpp</span> asserts all of it.</p>
</div>
{sections}
'''


if __name__ == "__main__":
    sys.stdout.write(page([Theme(t) for t in ORDER]))
