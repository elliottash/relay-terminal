# SPDX-License-Identifier: GPL-3.0-or-later
"""The phone half of live.sh: pair, open the shared pane, and act on it through the pane view.

Everything here goes through the real client (app/app.js mounting app/pane.js) against the real
hub, so what it proves is the whole path: the desktop pane published `pane_state`, the hub filtered
and fanned it out, the view drew the desktop's own rows, and a tap in the browser came back as an
action the pane carried out.

Usage: live_browser.py <pairing url> <screenshot dir>
"""
import asyncio
import base64
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tests.browser import Browser, shown   # noqa: E402


async def shot(browser: Browser, out: Path, name: str) -> None:
    data = await browser.call("Page.captureScreenshot", {"format": "png"})
    (out / f"implementer-live-{name}.png").write_bytes(base64.b64decode(data["data"]))


async def main(url: str, out: Path) -> None:
    browser = Browser(insecure=True)     # the desktop's development certificate
    await browser.start()
    try:
        # A phone-sized window, so the view draws the phone layout.
        await browser.call("Emulation.setDeviceMetricsOverride",
                           {"width": 390, "height": 844, "deviceScaleFactor": 3, "mobile": True})
        await browser.navigate(url)
        code = await browser.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/) "
            "? document.getElementById('pair-code').textContent : ''", timeout=60)
        print("phone shows code", code, flush=True)
        await browser.wait_for(shown('screen-inbox'), timeout=90)
        print("paired", flush=True)

        await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        print("pane open; waiting for the desktop's turn and its waiting message", flush=True)
        # The view mounts on the first pane_state the desktop publishes for this pane. Wait for
        # the state this run is about: the desktop's message waiting for the next tool call, which
        # is a queued row for the second between the Enter that queues it and the Enter that
        # steers it.
        await browser.wait_for(
            "[...document.querySelectorAll('.relay-pane .rp-row')]"
            ".some(e => e.dataset.kind === 'steer')", timeout=90)
        rows = json.loads(await browser.evaluate(
            "JSON.stringify([...document.querySelectorAll('.rp-row')]"
            ".map(e => [e.dataset.rowId, e.dataset.kind, e.querySelector('.rp-row-label').textContent]))"))
        print("phone sees rows:", json.dumps(rows), flush=True)
        print("phone sees clock:", await browser.evaluate(
            "document.querySelector('.rp-clock')?.textContent || ''"), flush=True)
        print("phone sees model:", await browser.evaluate(
            "document.querySelector('.rp-model')?.value !== undefined"
            " ? document.querySelector('.rp-model').options[0].textContent : ''"), flush=True)
        print("phone sees reasoning:", (await browser.evaluate(
            "document.querySelector('.rp-thinking-tail')?.textContent || ''"))[:80], flush=True)
        await shot(browser, out, "01-phone-drawing-the-desktops-pane")

        # Withdraw the waiting steer from the phone: the × on the row the desktop published.
        steer = next((row for row in rows if row[1] == "steer"), None)
        if steer is None:
            print("no steer row to withdraw", flush=True)
        else:
            await browser.evaluate(
                "document.querySelector('.rp-row[data-row-id=\"%s\"] .rp-row-x').click()" % steer[0])
            print("phone withdrew:", steer[0], flush=True)
            # The desktop answers with a fresh state, and the row goes.
            gone = await browser.wait_for(
                "!document.querySelector('.rp-row[data-row-id=\"%s\"]')" % steer[0], timeout=30)
            print("row gone from the phone:", bool(gone), flush=True)
        await asyncio.sleep(2)
        await shot(browser, out, "02-after-the-phone-withdrew-it")

        after = json.loads(await browser.evaluate(
            "JSON.stringify([...document.querySelectorAll('.rp-row')].map(e => e.dataset.rowId))"))
        print("rows after:", json.dumps(after), flush=True)
        print("console problems:", [line for line in browser.console if "EXCEPTION" in line], flush=True)
    finally:
        await browser.stop()


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(sys.argv[1], Path(sys.argv[2])), 300))
