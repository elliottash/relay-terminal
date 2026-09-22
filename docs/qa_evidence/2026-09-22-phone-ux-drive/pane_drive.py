# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bench A: the pane view (app/pane.js) at 390x844, driven for UX.

Reads the same fixtures tests/test_pane_view.py uses, through app/pane-demo.html, and takes a
screenshot of each thing a thumb would do: the prompt, the model menu, the effort chip, the
sessions sheet. Nothing here is a test; it is a drive that leaves pictures and a log.
"""
import asyncio
import base64
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tests.browser import Browser                      # noqa: E402
from tests.test_pane_view import serve                 # noqa: E402

OUT = Path(__file__).resolve().parent
PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}
log = []


def say(text):
    print(text, flush=True)
    log.append(text)


async def shot(browser, name):
    data = await browser.call("Page.captureScreenshot", {"format": "png"})
    path = OUT / f"{name}.png"
    path.write_bytes(base64.b64decode(data["data"]))
    return path


async def main():
    server, origin = serve(ROOT)
    browser = Browser()
    await browser.start()
    try:
        await browser.call("Emulation.setDeviceMetricsOverride", PHONE)
        await browser.call("Emulation.setTouchEmulationEnabled", {"enabled": True,
                                                                 "maxTouchPoints": 5})
        for fixture in ("idle", "busy_queue", "thinking_long_tail", "allowance", "view_only"):
            await browser.navigate(f"{origin}/app/pane-demo.html?fixture={fixture}&bare=1&input=touch")
            await browser.wait_for("document.body && document.body.dataset.demoReady === '1'"
                                   " && !!document.querySelector('.relay-pane')")
            await asyncio.sleep(0.4)
            await shot(browser, f"A-{fixture}")
            say(f"[{fixture}] drawn")

        # Back on idle: the chips the new work added.
        await browser.navigate(f"{origin}/app/pane-demo.html?fixture=idle&bare=1&input=touch")
        await browser.wait_for("document.body.dataset.demoReady === '1'")
        await asyncio.sleep(0.4)

        chips = await browser.evaluate(
            "JSON.stringify([...document.querySelectorAll('.rp-foot button, .rp-bar button,"
            " .rp-chip, [class*=chip]')].map(e => [e.className, (e.textContent||'').trim()]))")
        say("chips: " + chips)

        # The effort chip.
        found = await browser.evaluate(
            "(() => { const e = [...document.querySelectorAll('button')]"
            ".find(b => /effort|low|medium|high|minimal/i.test(b.className + ' ' + b.textContent));"
            " return e ? JSON.stringify({cls: e.className, text: e.textContent.trim(),"
            " rect: e.getBoundingClientRect().toJSON()}) : ''; })()")
        say("effort chip: " + str(found))
        if found:
            cls = json.loads(found)["cls"].split()[0]
            await browser.evaluate(f"document.querySelector('.{cls}').click()")
            await asyncio.sleep(0.4)
            await shot(browser, "A-effort-menu")
            say("effort menu open: " + await browser.evaluate(
                "JSON.stringify([...document.querySelectorAll('.rp-sheet button, dialog button,"
                " .rp-menu button')].map(e => e.textContent.trim()))"))
            await browser.evaluate("JSON.stringify(window.paneDemo.sent)")

        # The model menu.
        model = await browser.evaluate(
            "(() => { const e = document.querySelector('.rp-model, .rp-model-button,"
            " [class*=model]'); return e ? e.className : ''; })()")
        say("model element: " + str(model))

        await browser.evaluate("(() => { const t = document.querySelector('.rp-prompt textarea,"
                               " textarea'); if (t) { t.focus(); t.value = 'plan the next step';"
                               " t.dispatchEvent(new Event('input', {bubbles: true})); } })()")
        await asyncio.sleep(0.3)
        await shot(browser, "A-prompt-typed")
        say("sent so far: " + await browser.evaluate("JSON.stringify(window.paneDemo.sent)"))

        # The sessions (Conversations) sheet, with 50 in it.
        await browser.navigate(f"{origin}/app/pane-demo.html?fixture=sessions_50&bare=1&input=touch")
        await browser.wait_for("document.body.dataset.demoReady === '1'")
        await asyncio.sleep(0.4)
        await shot(browser, "A-sessions-pane")
        opened = await browser.evaluate(
            "(() => { const e = [...document.querySelectorAll('button')]"
            ".find(b => /conversation|session/i.test(b.className + ' ' + b.textContent));"
            " if (!e) return ''; e.click(); return e.className + ' | ' + e.textContent.trim(); })()")
        say("sessions opener: " + str(opened))
        await asyncio.sleep(0.6)
        await shot(browser, "A-sessions-sheet")
        say("console: " + json.dumps(browser.console[-10:]))
    finally:
        await browser.stop()
        server.shutdown()
        server.server_close()
        (OUT / "pane_drive.log").write_text("\n".join(log) + "\n")


asyncio.run(main())
