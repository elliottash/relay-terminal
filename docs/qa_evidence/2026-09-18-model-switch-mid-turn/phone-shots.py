#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Phone evidence for card 3ES1: the real web client (app/), in headless Chrome at a phone's size,
paired over the real rendezvous and host (the harness tests/test_remote_browser.py uses), fed the
model events of the gaps-driver.sh runs through the host's forwarding (remote/wire.py allow-list).

The demo pane source has no screen stream, so the client renders its own transcript: the lines
below are what a phone shows for an agent-only desktop; with a screen, the same lines arrive
printed in the terminal by the desktop, and the note and indicator are the same.

Run from the repo root: PYTHONPATH=backend:. python3 docs/qa_evidence/2026-09-18-model-switch-mid-turn/phone-shots.py
"""
import asyncio
import base64
from pathlib import Path

from tests.browser import Browser, shown
from tests.test_remote_browser import Harness

OUT = Path(__file__).resolve().parent
STEPS = [
    ("01-switch-accepted", [
        {"event": "model_changed", "model": "small", "applies": "next_step", "in_flight_model": "big",
         "context_window": 12000, "will_compact": True}]),
    ("02-landed-after-compaction", [
        {"event": "compaction_started", "reason": "model_switch", "for_model": "small"},
        {"event": "compacted", "reason": "model_switch", "for_model": "small", "before_tokens": 16380,
         "after_tokens": 4600},
        {"event": "model_applied", "at": "step", "step": 2, "model": "small", "from_model": "big",
         "context_window": 12000, "compacted": True}]),
    ("03-refused", [
        {"event": "model_changed", "model": "tiny", "applies": "next_step", "in_flight_model": "small",
         "context_window": 4096},
        {"event": "model_switch_refused", "at": "step", "model": "tiny", "current_model": "small",
         "reason": "tiny cannot take over: even compacted, the conversation needs about 5,100 tokens and "
                   "its 4,096-token window holds 3,072 with room for a reply. Staying on small."}]),
]


async def main():
    async with Harness() as harness:
        url, _ = await harness.host.open_pairing()
        browser = Browser()
        await browser.start()
        try:
            await browser.call("Emulation.setDeviceMetricsOverride",
                               {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True})
            await browser.navigate(url)
            await browser.wait_for(shown("screen-inbox"), timeout=60)
            await browser.wait_for("document.querySelectorAll('.pane-row').length > 0")
            await browser.evaluate("""[...document.querySelectorAll('.pane-row')].find(
                row => row.querySelector('.pane-title')?.textContent === 'relay-terminal').click()""")
            await browser.wait_for(shown("screen-thread"))
            count = 0
            for name, events in STEPS:
                for event in events:
                    harness.source._emit("pane-1", event)
                count += sum(1 for e in events if e["event"].startswith("model_"))
                await browser.wait_for(f"document.querySelectorAll('.model-line').length === {count}")
                await asyncio.sleep(0.3)
                shot = await browser.call("Page.captureScreenshot", {"format": "png"})
                (OUT / f"phone-{name}.png").write_bytes(base64.b64decode(shot["data"]))
            lines = await browser.evaluate("[...document.querySelectorAll('.model-line')].map(n => n.textContent)")
            indicator = await browser.evaluate("document.getElementById('thread-model').textContent")
            (OUT / "phone-lines.txt").write_text("\n".join(lines + ["indicator: " + indicator]) + "\n")
        finally:
            await browser.stop()


asyncio.run(asyncio.wait_for(main(), 180))
