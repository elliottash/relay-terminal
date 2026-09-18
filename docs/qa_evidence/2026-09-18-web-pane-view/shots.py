# SPDX-License-Identifier: GPL-3.0-or-later
"""Screenshots of the pane view (app/pane.js) at phone, tablet and laptop sizes.

Serves the repository, drives app/pane-demo.html in headless Chrome over the DevTools protocol
(the same harness tests/test_pane_view.py uses), and writes implementer-*.png beside this file.
Usage: python3 docs/qa_evidence/2026-09-18-web-pane-view/shots.py
"""
import asyncio
import base64
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

from tests.browser import Browser, find_chrome            # noqa: E402
from tests.test_pane_view import serve                    # noqa: E402

# width, height, the device the view should decide on, and the scale a real screen has
SIZES = [("phone", 390, 844, 3), ("tablet", 1024, 1366, 2), ("laptop", 1440, 900, 1)]
SHOTS = [("busy_queue", "mouse"), ("thinking_long_tail", "touch"), ("view_only", "mouse"),
         ("sessions_50", "touch")]


async def main() -> int:
    if not find_chrome():
        print("Chrome is not installed")
        return 1
    server, origin = serve(ROOT)
    browser = Browser()
    await browser.start()
    try:
        for name, input_kind in SHOTS:
            for device, width, height, scale in SIZES:
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": width, "height": height, "deviceScaleFactor": scale,
                                    "mobile": device == "phone"})
                await browser.navigate(
                    f"{origin}/app/pane-demo.html?fixture={name}&bare=1&input={input_kind}")
                await browser.wait_for("document.body.dataset.demoReady === '1'"
                                       " && !!document.querySelector('.relay-pane')")
                decided = await browser.evaluate("document.querySelector('.relay-pane').dataset.device")
                shot = await browser.call("Page.captureScreenshot", {"format": "png"})
                out = HERE / f"implementer-{name}-{device}.png"
                out.write_bytes(base64.b64decode(shot["data"]))
                print(f"{out.name}: {width}x{height} @{scale}x, the view drew for '{decided}'")
        print("console:", browser.console or "clean")
    finally:
        await browser.stop()
        server.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
