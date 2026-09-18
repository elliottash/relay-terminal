# SPDX-License-Identifier: GPL-3.0-or-later
"""Walk a guest through every state of the multiplayer client and photograph each one (#W5N2).

One headless Chrome at phone width (390×844), one real rendezvous and one real hub, from the join
screen to the end of the invitation. The owner's three answers — the knock, the keyboard and the
prompt — are driven from here, which is exactly where the desktop's sharing panel plugs in.

Run: python3 docs/qa_evidence/2026-09-18-remote-multiplayer-guest/browser_guest.py
"""
import asyncio
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from remote import wire                                       # noqa: E402
from tests.browser import Browser, shown                      # noqa: E402
from tests.test_remote_guest_browser import Harness           # noqa: E402

HERE = Path(__file__).resolve().parent


async def shot(browser: Browser, name: str) -> None:
    result = await browser.call("Page.captureScreenshot", {"format": "png",
                                                           "captureBeyondViewport": False})
    (HERE / f"guest-{name}.png").write_bytes(base64.b64decode(result["data"]))
    print("shot", name, flush=True)


async def main() -> None:
    # The owner answers when these are set, and not before, so the waiting states hold still
    # long enough to be photographed.
    admit = asyncio.Event()
    grant = asyncio.Event()
    decide_prompt = asyncio.Event()

    async with Harness(role=wire.VIEWER) as harness:
        async def knock_approver(request):
            harness.knocks.append(request)
            await admit.wait()
            return True, wire.VIEWER

        async def control_approver(request):
            harness.control_asks.append(request)
            await grant.wait()
            return True

        async def prompt_approver(request):
            harness.prompt_asks.append(request)
            await decide_prompt.wait()
            return True

        harness.host.knock_approver = knock_approver
        harness.host.control_approver = control_approver
        harness.host.prompt_approver = prompt_approver

        url = await harness.invite(role=wire.EDITOR)
        browser = Browser()
        await browser.start()
        try:
            await browser.call("Emulation.setDeviceMetricsOverride", {
                "width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True})

            # 1. The link opens on the join screen: who, their key, and a name to knock with.
            await browser.navigate(url)
            await browser.wait_for(shown('screen-join'), timeout=40)
            await browser.wait_for(
                "document.getElementById('join-fingerprint').textContent !== '…'")
            print("hash after load:", repr(await browser.evaluate("location.hash")), flush=True)
            await browser.evaluate(
                "(() => { document.getElementById('join-name').value = 'Bo'; return true; })()")
            await shot(browser, "01-join")

            # 2. Knocking: the five digits, and a countdown that says how long they have.
            await browser.evaluate("document.getElementById('join-knock').click()")
            code = await browser.wait_for(
                "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
                " ? document.getElementById('knock-code').textContent : ''", timeout=40)
            print("phone shows", code, "· hub derived", harness.knocks[0].code, flush=True)
            await asyncio.sleep(2.2)              # let the countdown show a real number
            await shot(browser, "02-waiting")

            # 3. Admitted as a viewer: the pane, and nothing to type into.
            admit.set()
            await browser.wait_for(shown('screen-guest'), timeout=60)
            await browser.wait_for(
                "[...document.querySelectorAll('#guest-screen-wrap .screen-row')]"
                ".some(n => n.textContent.includes('row-'))", timeout=30)
            await asyncio.sleep(0.5)
            print("composer drawn for a viewer:",
                  await browser.evaluate(shown('guest-composer')), flush=True)
            await shot(browser, "03-viewer")

            # 4. Promoted to editor, asks for the keyboard, and is given it.
            participant = harness.participant().participant_id
            await harness.host.role_set(participant, wire.EDITOR)
            await browser.wait_for(shown('guest-ask'), timeout=30)
            await browser.evaluate("document.getElementById('guest-ask').click()")
            await browser.wait_for(
                "document.getElementById('guest-drive-mode').textContent === 'Asked to type…'",
                timeout=30)
            await shot(browser, "04-asked-to-type")
            grant.set()
            await browser.wait_for(shown('guest-hand-back'), timeout=30)
            await browser.evaluate(
                "(() => { document.getElementById('guest-line').value = 'ls -l';"
                " document.getElementById('guest-line-send').click(); return true; })()")
            await asyncio.sleep(0.4)
            await shot(browser, "05-editor-driving")

            # 5. A prompt, waiting for the owner to approve it.
            await browser.evaluate(
                "(() => { document.getElementById('guest-prompt-text').value ="
                " 'can you run the remote tests and tell me what fails?';"
                " document.getElementById('guest-prompt-send').click(); return true; })()")
            await browser.wait_for(
                "document.querySelector('.guest-prompt .guest-prompt-state')?.textContent"
                ".includes('waiting') || false", timeout=30)
            await shot(browser, "06-prompt-pending")
            decide_prompt.set()
            await browser.wait_for(
                "document.querySelector('.guest-prompt.is-approved') !== null", timeout=30)
            await shot(browser, "07-prompt-approved")

            # 6. The owner takes the keyboard back with their own keystroke.
            await browser.evaluate(
                "(() => { document.getElementById('guest-line').value = 'rm -r bui';"
                " return true; })()")
            harness.host.take_control("pane-1")
            await browser.wait_for(f"!({shown('guest-hand-back')})", timeout=30)
            print("half-typed line kept:",
                  repr(await browser.evaluate(
                      "document.getElementById('guest-line').value")), flush=True)
            await shot(browser, "08-owner-took-it-back")

            # 7. The owner pauses guests.
            harness.host.share_pause("pane-1", True)
            await browser.wait_for(
                "document.getElementById('guest-note').textContent.includes('paused')",
                timeout=30)
            await shot(browser, "09-paused")
            harness.host.share_pause("pane-1", False)

            # 8. And removes them: the screen stops and says so.
            await harness.host.participant_remove(participant)
            await browser.wait_for(shown('screen-ended'), timeout=40)
            await shot(browser, "10-ended")

            problems = [line for line in browser.console
                        if "EXCEPTION" in line or "error:" in line.lower()]
            print("console problems:", problems, flush=True)
        finally:
            await browser.stop()


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(), 300))
