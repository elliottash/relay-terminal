# SPDX-License-Identifier: GPL-3.0-or-later
"""The phone half of the keyboard drive: an iPad-sized browser, paired with a real Relay, with
Safari's on-screen keyboard reproduced as Safari does it.

Owner, 2026-09-18, on an iPad: "it looks good in portrait but not landscape, it goes off screen",
then "same on portrait actually, test it with the on screen keyboard".

There is no iPad here, so this reproduces what Safari's keyboard does to a page rather than
imitating a keyboard: the **layout** viewport keeps the whole screen (so `100vh`, `innerHeight`
and a body at 100% all still mean the whole screen), the **visual** viewport — the part above the
keyboard — shrinks by the keyboard's height, and Safari scrolls the page so the focused box is in
view. `visualViewport.height` and `offsetTop` are overridden to those numbers and its `resize` and
`scroll` events fired, which is everything app/viewport.js listens to.

For each device and orientation the drive focuses the pane's prompt box, raises the keyboard, and
measures whether the top bar, the terminal, the prompt box and its send button are inside the
visible rectangle. It prints one line per case, takes a screenshot of each, and exits 1 when any
of them is off screen. A real iPad is still the final word; this is the regression guard.

Usage: keyboard_browser.py <pairing url> <screenshot dir>
"""
import asyncio
import base64
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tests.browser import Browser, shown   # noqa: E402

# width, height, keyboard height (CSS px). The screen sizes are the devices' own; the keyboard
# heights are typical values for Safari's keyboard with its suggestion bar, NOT measured on the
# owner's devices — the tall-keyboard cases exist because a real one can run taller. Measuring one
# is `innerHeight - visualViewport.height` in Safari's console with a text field focused; if a real
# device reports more, add it here.
CASES = [
    ("ipad-portrait", 820, 1180, 398),
    # The owner saw portrait fail on his iPad where the first matrix passed it: a taller keyboard
    # (the shortcut and suggestion bars) and the other iPad sizes are the likely difference.
    ("ipad-portrait-tall-keyboard", 820, 1180, 520),
    ("ipad-mini-portrait", 744, 1133, 430),
    ("ipad-pro-11-portrait", 834, 1194, 520),
    ("ipad-landscape", 1180, 820, 460),
    ("ipad-pro-11-landscape", 1194, 834, 480),
    ("iphone-portrait", 390, 844, 336),
    ("iphone-landscape", 844, 390, 205),
]

# What must stay on screen with the keyboard up, by selector, and why a person needs it.
MUST_SEE = [
    ("#thread-bar", "the bar: which pane this is, and the way back"),
    (".relay-pane .rp-terminal", "some of the terminal"),
    (".relay-pane .rp-input", "the prompt box being typed in"),
    (".relay-pane .rp-send", "the send button"),
]

KEYBOARD_UP = """(async (keyboard) => {
  const vv = window.visualViewport;
  const full = window.innerHeight;
  const visible = full - keyboard;
  // Safari scrolls so the focused box sits above the keyboard; the visual viewport's offsetTop is
  // how far. Work out what Safari would: enough to bring the box's bottom above the keyboard.
  const box = document.querySelector('.relay-pane .rp-input');
  box.focus();
  const bottom = box.getBoundingClientRect().bottom;
  const offset = Math.max(0, Math.min(bottom - visible + 8, full - visible));
  Object.defineProperty(vv, 'height', { configurable: true, get: () => visible });
  Object.defineProperty(vv, 'offsetTop', { configurable: true, get: () => offset });
  window.scrollTo(0, offset);
  vv.dispatchEvent(new Event('resize'));
  vv.dispatchEvent(new Event('scroll'));
  await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
  return { full, visible, offset };
})"""

MEASURE = """((selectors) => {
  const vv = window.visualViewport;
  const top = vv.offsetTop, bottom = vv.offsetTop + vv.height;
  return selectors.map((s) => {
    const e = document.querySelector(s);
    if (!e) return [s, 'missing', 0, 0];
    const r = e.getBoundingClientRect();
    // Page coordinates, so they compare with the visual viewport's own.
    const y0 = r.top + window.scrollY, y1 = r.bottom + window.scrollY;
    const drawn = r.width > 0 && r.height > 0 && getComputedStyle(e).visibility !== 'hidden';
    const inside = drawn && y0 >= top - 1 && y1 <= bottom + 1;
    return [s, drawn ? (inside ? 'inside' : 'off screen') : 'not drawn', Math.round(y0), Math.round(y1)];
  });
})"""


async def main(url: str, out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    browser = Browser(insecure=True)
    await browser.start()
    failures = 0
    try:
        await browser.call("Emulation.setDeviceMetricsOverride",
                           {"width": 820, "height": 1180, "deviceScaleFactor": 2, "mobile": True})
        await browser.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5})
        await browser.navigate(url)
        code = await browser.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/) "
            "? document.getElementById('pair-code').textContent : ''", timeout=60)
        print("phone shows code", code, flush=True)
        await browser.wait_for(shown('screen-inbox'), timeout=90)
        await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await browser.wait_for("!!document.querySelector('.relay-pane .rp-input')", timeout=60)
        print("pane view mounted", flush=True)

        for name, width, height, keyboard in CASES:
            await browser.call("Emulation.setDeviceMetricsOverride",
                               {"width": width, "height": height, "deviceScaleFactor": 2,
                                "mobile": True})
            # Start from a clean page for each case: keyboard down, nothing scrolled, nothing
            # overridden from the case before.
            await browser.evaluate("(() => { const vv = window.visualViewport;"
                                   " delete vv.height; delete vv.offsetTop; window.scrollTo(0, 0);"
                                   " vv.dispatchEvent(new Event('resize')); return true; })()")
            await asyncio.sleep(0.4)
            before = json.loads(await browser.evaluate(f"JSON.stringify(({MEASURE})({json.dumps([s for s, _ in MUST_SEE])}))"))
            geometry = await browser.evaluate(f"({KEYBOARD_UP})({keyboard})", timeout=20)
            await asyncio.sleep(0.4)
            after = json.loads(await browser.evaluate(f"JSON.stringify(({MEASURE})({json.dumps([s for s, _ in MUST_SEE])}))"))
            shot = await browser.call("Page.captureScreenshot", {"format": "png"})
            (out / f"implementer-{name}-keyboard-up.png").write_bytes(base64.b64decode(shot["data"]))
            bad = [(s, where) for (s, where, _, _) in after if where != "inside"]
            failures += bool(bad)
            print(f"{name}: {width}x{height}, keyboard {keyboard}px -> visible {geometry['visible']}px "
                  f"(scrolled {geometry['offset']}): "
                  + ("all inside" if not bad else "OFF: " + ", ".join(f"{s} {w}" for s, w in bad)),
                  flush=True)
            unfocused_bad = [s for (s, where, _, _) in before if where != "inside"]
            if unfocused_bad:
                print(f"   (even with the keyboard down: {', '.join(unfocused_bad)} not inside)", flush=True)
        print("console problems:", [line for line in browser.console if "EXCEPTION" in line], flush=True)
    finally:
        await browser.stop()
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(asyncio.wait_for(main(sys.argv[1], Path(sys.argv[2])), 300)))
