# SPDX-License-Identifier: AGPL-3.0-or-later
"""The pane view after #EFT9, #KBD7, #CPY4 and #PKT5 item 4, driven at 390x844.

The same bench as docs/qa_evidence/2026-09-22-phone-ux-drive/pane_drive.py — app/pane-demo.html
over the tests/fixtures/pane_state/ fixtures in a real headless Chrome with touch emulation — but
pointed at the four things the drive of 2026-09-22 measured wrong. It is not a test: it takes the
same measurements again and leaves the numbers and the pictures beside them.

    python3 docs/qa_evidence/2026-09-22-streamA-pane/drive.py
"""
import asyncio
import base64
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tests.browser import Browser                      # noqa: E402
from tests.test_pane_view import serve, fixture        # noqa: E402

OUT = Path(__file__).resolve().parent
PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}
log = []


def say(text):
    print(text, flush=True)
    log.append(text)


async def shot(browser, name):
    data = await browser.call("Page.captureScreenshot", {"format": "png"})
    (OUT / f"{name}.png").write_bytes(base64.b64decode(data["data"]))


async def open_fixture(browser, origin, name, *, query="&input=touch"):
    await browser.navigate(f"{origin}/app/pane-demo.html?fixture={name}&bare=1{query}")
    await browser.wait_for("document.body && document.body.dataset.demoReady === '1'"
                           " && !!document.querySelector('.relay-pane')")
    await asyncio.sleep(0.4)


async def main():
    server, origin = serve(ROOT)
    browser = Browser()
    await browser.start()
    try:
        await browser.call("Emulation.setDeviceMetricsOverride", PHONE)
        await browser.call("Emulation.setTouchEmulationEnabled",
                           {"enabled": True, "maxTouchPoints": 5})

        # ---- #EFT9: the chip's words, its chevron, and a level-only state -------------------
        await open_fixture(browser, origin, "idle")
        say("EFT9 closed chip: " + await browser.evaluate(
            "(() => { const e = document.querySelector('.rp-effort');"
            " return JSON.stringify({value: e.value,"
            " shown: e.options[e.selectedIndex].textContent,"
            " opts: [...e.options].map(o => o.textContent)}); })()"))
        say("EFT9 geometry: " + await browser.evaluate("""
          (() => { const r = (s) => { const b = document.querySelector(s)
            .getBoundingClientRect(); return [Math.round(b.left * 10) / 10,
            Math.round(b.right * 10) / 10]; };
            return JSON.stringify({model: r('.rp-model'),
              modelChevron: r('.rp-model-box .rp-model-chevron'),
              effort: r('.rp-effort'), effortChevron: r('.rp-effort-chevron')}); })()
        """))
        await shot(browser, "EFT9-chip")
        state = fixture("idle")
        lower = dict(state, seq=state["seq"] + 1, model=dict(state["model"], effort="low"))
        await browser.evaluate(f"window.paneDemo.update({json.dumps(lower)})")
        await asyncio.sleep(0.2)
        say("EFT9 after effort=low (model unchanged): " + await browser.evaluate(
            "(() => { const e = document.querySelector('.rp-effort');"
            " return JSON.stringify({value: e.value,"
            " shown: e.options[e.selectedIndex].textContent,"
            " opts: [...e.options].map(o => o.textContent)}); })()"))
        await shot(browser, "EFT9-chip-after-level-only-state")
        fixed = dict(state, seq=state["seq"] + 2, model=dict(state["model"], effort_fixed=True))
        await browser.evaluate(f"window.paneDemo.update({json.dumps(fixed)})")
        await asyncio.sleep(0.2)
        say("EFT9 effort_fixed: " + await browser.evaluate(
            "JSON.stringify({disabled: document.querySelector('.rp-effort').disabled,"
            " chevron: getComputedStyle(document.querySelector('.rp-effort-chevron')).display})"))
        await shot(browser, "EFT9-chip-fixed")

        # ---- #KBD7: Send on a thumb, and Send under a physical keyboard --------------------
        for kind in ("touch", "mouse"):
            await open_fixture(browser, origin, "idle", query=f"&input={kind}")
            await browser.evaluate("""
              (() => { const b = document.querySelector('.rp-input'); b.focus();
                b.value = 'plan the next step';
                b.dispatchEvent(new Event('input', {bubbles: true})); })()
            """)
            await asyncio.sleep(0.2)
            await browser.evaluate("document.querySelector('.rp-send').click()")
            await asyncio.sleep(0.3)
            say(f"KBD7 [{kind}] after Send: focus=" + await browser.evaluate(
                "document.activeElement ? document.activeElement.className : ''")
                + " box=" + json.dumps(await browser.evaluate(
                    "document.querySelector('.rp-input').value")))
            await shot(browser, f"KBD7-send-{kind}")

        # ---- #CPY4: the toast over the sheet, the id in the tap, the list's scroll ----------
        await open_fixture(browser, origin, "sessions_50")
        sessions = fixture("sessions_50")
        row = sessions["sessions"]["rows"][0]
        await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
        await browser.wait_for("!!document.querySelector('.rp-session-list')")
        await browser.evaluate("""
          (() => { let copied = '';
            Object.defineProperty(navigator, 'clipboard',
              { value: { writeText: (t) => { copied = t; return Promise.resolve(); } },
                configurable: true });
            window.paneDemo.copied = () => copied; })()
        """)
        sel = (".rp-session-row[data-session-id=\"%s\"] .rp-session-copy" % row["id"])
        await browser.evaluate(
            "(() => { const b = document.querySelector('%s');"
            " b.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true,"
            " pointerType: 'touch'})); })()" % sel)
        await browser.wait_for("window.paneDemo.sent.length > 0")
        asked = json.loads(await browser.evaluate("JSON.stringify(window.paneDemo.sent)"))[-1]
        say("CPY4 asked on the press: " + json.dumps(asked))
        conversation = "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"
        await browser.evaluate(
            "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
            " session: %s, conversation: %s, id: %s})"
            % (json.dumps(sessions["pane"]), json.dumps(row["id"]),
               json.dumps(conversation), json.dumps(asked["id"])))
        say("CPY4 clipboard before the tap: "
            + json.dumps(await browser.evaluate("window.paneDemo.copied()")))
        await browser.evaluate("document.querySelector('%s').click()" % sel)
        await asyncio.sleep(0.3)
        say("CPY4 clipboard written inside the tap: "
            + json.dumps(await browser.evaluate("window.paneDemo.copied()")))
        say("CPY4 toast: " + json.dumps(await browser.evaluate("window.paneDemo.toast()")))
        topmost = """
          (() => { const t = document.querySelector('.rp-toast');
            const b = t.getBoundingClientRect();
            const at = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2);
            return JSON.stringify({toastHidden: t.hidden,
              topmost: at ? (t.contains(at) ? 'the toast (' + at.className + ')'
                                            : at.className) : null,
              sheetOpen: !document.querySelector('.rp-layer').hidden}); })()
        """
        say("CPY4 topmost at the toast's centre, sheet closed by the tap: "
            + await browser.evaluate(topmost))
        await shot(browser, "CPY4-toast-over-the-sheet")

        # The same measurement the review took: the toast up with a sheet open over the pane. The
        # clipboard refuses, which is the iOS path, and the answer lands after the list is back up.
        await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
        await browser.wait_for("!!document.querySelector('.rp-session-list')")
        await browser.evaluate("""
          Object.defineProperty(navigator, 'clipboard',
            { value: { writeText: () => Promise.reject(new Error('no')) }, configurable: true })
        """)
        second = sessions["sessions"]["rows"][1]
        sel2 = (".rp-session-row[data-session-id=\"%s\"] .rp-session-copy" % second["id"])
        await browser.evaluate("document.querySelector('%s').click()" % sel2)
        await browser.wait_for("window.paneDemo.sent.length > 1")
        asked2 = json.loads(await browser.evaluate("JSON.stringify(window.paneDemo.sent)"))[-1]
        await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
        await browser.wait_for("!!document.querySelector('.rp-session-list')")
        await browser.evaluate(
            "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
            " session: %s, conversation: %s, id: %s})"
            % (json.dumps(sessions["pane"]), json.dumps(second["id"]),
               json.dumps(conversation), json.dumps(asked2["id"])))
        await browser.wait_for("!document.querySelector('.rp-toast').hidden")
        await asyncio.sleep(0.2)
        say("CPY4 refused write, sheet open: " + await browser.evaluate(topmost))
        say("CPY4 the refusal carries the id: "
            + json.dumps(await browser.evaluate("window.paneDemo.toast()"))
            + " sheet lines: " + await browser.evaluate(
                "String(document.querySelectorAll('.rp-sheet .rp-session-id').length)"))
        await shot(browser, "CPY4-refused-id-over-an-open-sheet")

        # The list holds its scroll while only the clock moves.
        await open_fixture(browser, origin, "sessions_50")
        await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
        await browser.wait_for("!!document.querySelector('.rp-session-list')")
        await browser.evaluate("document.querySelector('.rp-sheet').scrollTop = 2010")
        await asyncio.sleep(0.2)
        before = await browser.evaluate("document.querySelector('.rp-sheet').scrollTop")
        focus_before = await browser.evaluate(
            "document.activeElement ? document.activeElement.className : ''")
        await browser.evaluate(
            "document.querySelectorAll('.rp-session-row')[3].dataset.driveMark = '1'")
        ticked = json.loads(json.dumps(sessions))
        ticked["seq"] += 1
        for r in ticked["sessions"]["rows"]:
            r["when"] = "one minute later"
        await browser.evaluate(f"window.paneDemo.update({json.dumps(ticked)})")
        await asyncio.sleep(0.3)
        say("CPY4 a minute tick: scrollTop %s -> %s, focus %r -> %r, row node kept: %s,"
            " when now %s" % (
                before,
                await browser.evaluate("document.querySelector('.rp-sheet').scrollTop"),
                focus_before,
                await browser.evaluate(
                    "document.activeElement ? document.activeElement.className : ''"),
                await browser.evaluate(
                    "String(!!document.querySelector('.rp-session-row[data-drive-mark]'))"),
                json.dumps(await browser.evaluate(
                    "document.querySelector('.rp-session-when').textContent"))))
        await shot(browser, "CPY4-list-holds-its-scroll")

        # ---- #PKT5 item 4: an ask option across an intervening pane_state -------------------
        await open_fixture(browser, origin, "busy_queue")
        busy = fixture("busy_queue")
        await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps({
            "t": "agent", "pane": busy["pane"],
            "event": {"event": "question", "id": "q1", "questions": [
                {"header": "Which branch?", "question": "Where should this land?",
                 "options": [{"label": "main", "recommended": True}, {"label": "a new branch"}]}]}}))
        await asyncio.sleep(0.3)
        await browser.evaluate("""
          (() => { const b = document.querySelector('.rp-ask-choice');
            b.dataset.driveMark = '1'; window.paneDemo.askNode = b; })()
        """)
        for n in range(10):
            later = dict(busy, seq=busy["seq"] + 1 + n)
            await browser.evaluate(f"window.paneDemo.update({json.dumps(later)})")
        await asyncio.sleep(0.2)
        say("PKT5 the ask button survived ten states: " + await browser.evaluate(
            "JSON.stringify({sameNode: window.paneDemo.askNode === "
            "document.querySelector('.rp-ask-choice'),"
            " marked: !!document.querySelector('.rp-ask-choice[data-drive-mark]')})"))
        sent_before = len(json.loads(await browser.evaluate(
            "JSON.stringify(window.paneDemo.sent)")))
        await browser.evaluate("window.paneDemo.askNode.click()")
        await asyncio.sleep(0.2)
        after = json.loads(await browser.evaluate("JSON.stringify(window.paneDemo.sent)"))
        say("PKT5 the tap still fires: " + json.dumps(after[sent_before:]))
        await shot(browser, "PKT5-ask-across-states")
        say("console: " + json.dumps(browser.console[-10:]))
    finally:
        await browser.stop()
        server.shutdown()
        server.server_close()
        (OUT / "drive.log").write_text("\n".join(log) + "\n")


asyncio.run(main())
