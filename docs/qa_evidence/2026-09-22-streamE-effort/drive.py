# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #EFT9, stream E: the whole seam driven end to end, at 390x844.

Nothing here is a fixture of the fields under test. Every state the phone is given comes out of
`effort_probe` — the desktop's own `relay::panestate::build()`, linked against
`librelay-panestate.a` — and is then run through `remote/pane_state.py` exactly as the hub runs it,
capability strip included, before it reaches the real `app/pane.js` in headless Chrome. So what is
measured is the seam between the three halves of this card, not three imitations of it.

    scripts/relay-build --target relay-panestate-tests      # librelay-panestate.a
    g++ -std=c++20 -fPIC $(pkg-config --cflags Qt5Core) -Isrc \
        -o /tmp/effort_probe docs/qa_evidence/2026-09-22-streamE-effort/effort_probe.cpp \
        build/librelay-panestate.a $(pkg-config --libs Qt5Core)
    python3 docs/qa_evidence/2026-09-22-streamE-effort/drive.py /tmp/effort_probe
"""
import asyncio
import base64
import copy
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from remote import pane_state, wire                    # noqa: E402
from tests.browser import Browser                      # noqa: E402
from tests.test_pane_view import serve, fixture        # noqa: E402

OUT = Path(__file__).resolve().parent
PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}
CHIP = ("(() => { const e = document.querySelector('.rp-effort');"
        " return JSON.stringify({hidden: e.hidden, disabled: e.disabled,"
        " shown: e.options[e.selectedIndex].textContent,"
        " opts: [...e.options].map(o => o.textContent),"
        " chevron: getComputedStyle(document.querySelector('.rp-effort-chevron')).display}); })()")
log = []


def say(text):
    print(text, flush=True)
    log.append(text)


async def shot(browser, name):
    data = await browser.call("Page.captureScreenshot", {"format": "png"})
    (OUT / f"{name}.png").write_bytes(base64.b64decode(data["data"]))


def desktop_blocks(binary):
    """What relay::panestate::build() puts in `model`, per case, from the real builder."""
    out = subprocess.run([binary], capture_output=True, text=True, check=True).stdout
    return [line.split("\t", 1) for line in out.strip().splitlines()]


def through_the_hub(block, seq, capability=wire.AGENT):
    """The desktop's block, cleaned and stripped the way the hub does it, as a whole state."""
    message = copy.deepcopy(fixture("idle"))
    message["model"] = json.loads(block)
    state = pane_state.for_capability(pane_state.clean(message), capability)
    state["seq"] = seq
    return state


async def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "/tmp/claude-1000/streamE/ev/effort_probe"
    blocks = desktop_blocks(binary)
    server, origin = serve(ROOT)
    browser = Browser()
    await browser.start()
    try:
        await browser.call("Emulation.setDeviceMetricsOverride", PHONE)
        await browser.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5})
        await browser.navigate(f"{origin}/app/pane-demo.html?fixture=idle&bare=1&input=touch")
        await browser.wait_for("document.body && document.body.dataset.demoReady === '1'"
                               " && !!document.querySelector('.relay-pane')")
        await asyncio.sleep(0.4)
        for index, (what, block) in enumerate(blocks, start=100):
            state = through_the_hub(block, index)
            say(f"=== {what}")
            say("  desktop " + block)
            say("  hub     " + json.dumps({k: v for k, v in state["model"].items() if "effort" in k}))
            await browser.evaluate(f"window.paneDemo.update({json.dumps(state)})")
            await asyncio.sleep(0.25)
            say("  phone   " + await browser.evaluate(CHIP))
            await shot(browser, f"EFT9-{what}")
        # A viewer has no level at all, so the chip is not drawn: the same state, `view`.
        state = through_the_hub(blocks[1][1], 200, wire.VIEW)
        say("=== relay-free as a viewer sees it")
        say("  hub     " + json.dumps({k: v for k, v in state["model"].items() if "effort" in k}))
        await browser.evaluate(f"window.paneDemo.update({json.dumps(state)})")
        await asyncio.sleep(0.25)
        say("  phone   " + await browser.evaluate(CHIP))
        await shot(browser, "EFT9-viewer")
    finally:
        await browser.stop()
        server.shutdown()
        (OUT / "drive.log").write_text("\n".join(log) + "\n")


if __name__ == "__main__":
    asyncio.run(main())
