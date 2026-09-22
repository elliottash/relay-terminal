# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bench B: the whole web app at 390x844 against a real host, driven for UX.

The rendezvous, the host and the Noise link are the real ones (tests/test_remote_browser.py's
Harness); the desktop behind them is a demo source whose terminal rows carry the things the
recent work claims to handle — http and https URLs, a www one, a URL in a sentence, a bracketed
one, a file path, and a line wider than the screen.
"""
import asyncio
import base64
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from remote import wire                                              # noqa: E402
from tests.browser import Browser, shown, SCREENS_SHOWN              # noqa: E402
from tests.test_remote_browser import Harness, ScrollbackSource      # noqa: E402

OUT = Path(__file__).resolve().parent
PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}
log = []

# Absolute row -> what that row says. Everything else is `row-N`, so a hole is still visible.
LINES = {
    3: "  see https://relay-terminal.ai/docs/remote for the protocol.",
    4: "  http://127.0.0.1:8080/health (the fake model) answers ok.",
    5: "  www.example.com has no scheme of its own.",
    6: "  the run is at https://github.com/relay/relay-terminal/pull/12#issue-7, review it.",
    7: "  (see https://example.com/a_(b)_c) and the note after it.",
    8: "  traceback: File \"backend/relay_core/router.py\", line 88, in route",
    9: "  " + "wide-" * 40 + "end",
    10: "  mailto:e@elliottash.com and ftp://old.example.com are not http.",
    11: "  https://example.com/path?q=1&r=2#frag, then a comma.",
}


class LinkSource(ScrollbackSource):
    """The same numbered stream, with a few rows that say something a thumb might tap."""

    @classmethod
    def _row(cls, screen_row: int, absolute: int) -> dict:
        text = LINES.get(absolute)
        if text is None:
            return ScrollbackSource._row(screen_row, absolute)
        return {"row": screen_row, "segs": [[text, 0, 0, 0]]}


def say(text):
    print(text, flush=True)
    log.append(str(text))


async def shot(browser, name):
    data = await browser.call("Page.captureScreenshot", {"format": "png"})
    (OUT / f"{name}.png").write_bytes(base64.b64decode(data["data"]))


async def main():
    async with Harness(capability=wire.AGENT, source=LinkSource) as harness:
        # The live block starts at row 0, so the URL rows above are on the first screen.
        harness.source.total = 0
        url, _ = await harness.host.open_pairing()
        browser = Browser()
        await browser.start()
        try:
            await browser.call("Emulation.setDeviceMetricsOverride", PHONE)
            await browser.call("Emulation.setTouchEmulationEnabled",
                               {"enabled": True, "maxTouchPoints": 5})
            await browser.navigate(url)
            await browser.wait_for(shown('screen-inbox'), timeout=60)
            await asyncio.sleep(0.6)
            await shot(browser, "B-01-inbox")
            say("inbox drawn; screens shown = %s" % await browser.evaluate(SCREENS_SHOWN))

            await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
            await browser.wait_for(shown('screen-thread'))
            await browser.wait_for(shown('terminal-pane'), timeout=30)
            await browser.wait_for("[...document.querySelectorAll('#screen-wrap .screen-row')]"
                                   ".some(n => n.textContent.includes('http'))", timeout=30)
            await asyncio.sleep(0.8)
            await shot(browser, "B-02-terminal")

            # What the link painter made of each row.
            links = await browser.evaluate(
                "JSON.stringify([...document.querySelectorAll('#screen-wrap a')]"
                ".map(a => [a.textContent, a.getAttribute('href'), a.getAttribute('target'),"
                " a.getAttribute('rel')]))")
            say("links: " + links)

            rows = await browser.evaluate(
                "JSON.stringify([...document.querySelectorAll('#screen-wrap .screen-row')]"
                ".filter(n => /http|www|wide-|router/.test(n.textContent))"
                ".map(n => n.textContent))")
            say("rows with something in them: " + rows)

            # The font floor and the A-/A+ buttons.
            font = "(() => { const g = document.querySelector('.screen-grid');"\
                   " return g ? getComputedStyle(g).fontSize : ''; })()"
            say("font at rest: " + str(await browser.evaluate(font)))
            say("A- present: %s  A+ present: %s" % (await browser.evaluate(shown('term-font-smaller')),
                                                    await browser.evaluate(shown('term-font-bigger'))))
            for _ in range(4):
                await browser.evaluate("document.getElementById('term-font-bigger').click()")
                await asyncio.sleep(0.15)
            say("font after four A+: " + str(await browser.evaluate(font)))
            await shot(browser, "B-03-font-bigger")
            wide = await browser.evaluate(
                "(() => { const w = document.getElementById('screen-wrap');"
                " return JSON.stringify({scrollWidth: w.scrollWidth, clientWidth: w.clientWidth,"
                " overflowX: getComputedStyle(w).overflowX}); })()")
            say("after A+: " + wide)
            for _ in range(8):
                await browser.evaluate("document.getElementById('term-font-smaller').click()")
                await asyncio.sleep(0.12)
            say("font after eight A-: " + str(await browser.evaluate(font)))
            say("stored floor: " + str(await browser.evaluate(
                "window.localStorage.getItem('relay.term-font-floor')")))
            await shot(browser, "B-04-font-smaller")

            # Reload: does the reader's choice survive?
            await browser.navigate(url.split('/pair#')[0] + '/')
            await browser.wait_for(shown('screen-inbox'), timeout=60)
            await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
            await browser.wait_for(shown('terminal-pane'), timeout=30)
            await asyncio.sleep(1.0)
            say("font after reload: " + str(await browser.evaluate(font)))

            # The prompt: type, send, and see whether the keyboard would come down.
            await browser.evaluate(
                "(() => { const t = document.querySelector('#thread-prompt, textarea');"
                " t.focus(); t.value = 'ls -la'; t.dispatchEvent(new Event('input', {bubbles:true}));"
                " })()")
            await asyncio.sleep(0.3)
            say("focused before send: " + str(await browser.evaluate(
                "document.activeElement && document.activeElement.tagName")))
            await shot(browser, "B-05-prompt-typed")
            await browser.evaluate(
                "(() => { const b = document.getElementById('prompt-send')"
                " || [...document.querySelectorAll('button')].find(e => /send/i.test(e.textContent));"
                " if (b) b.click(); return !!b; })()")
            await asyncio.sleep(0.6)
            say("focused after send: " + str(await browser.evaluate(
                "document.activeElement && document.activeElement.tagName")))
            say("typed at the desktop: " + json.dumps(
                [str(x) for x in getattr(harness.source, 'typed', [])][-3:]))
            await shot(browser, "B-06-after-send")

            say("console: " + json.dumps(browser.console[-12:]))
        finally:
            await browser.stop()
            (OUT / "app_drive.log").write_text("\n".join(log) + "\n")


asyncio.run(main())
