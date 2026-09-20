"""before/after cost of one streamed reply, against whichever app/ is served."""
import asyncio, json, sys
from pathlib import Path
sys.path.insert(0, '/home/elliott/repos/relay-terminal')
from tests.browser import Browser
from tests.test_pane_view import serve

ROOT = Path(sys.argv[1])

async def main():
    server, origin = serve(ROOT)
    b = Browser(); await b.start()
    try:
        await b.navigate(f"{origin}/tests/screen_harness.html")
        await b.wait_for("document.body.dataset.harnessReady === '1'")
        await b.evaluate("screenHarness.open({})")
        await b.wait_for("screenHarness.report().held >= 80", timeout=20)
        await b.evaluate("screenHarness.zero()")
        await b.evaluate("screenHarness.stream(200, 10)", timeout=60)
        r = await b.evaluate("screenHarness.report()")
        print(json.dumps({"root": str(ROOT), "frames": 200, "seconds": 2.0,
                          "history_get": r["asks"], "answered": r["answers"],
                          "getComputedStyle(root)": r["style"],
                          "clientWidth(root)": r["clientWidth"],
                          "rate_limited_at_120_per_60s": max(0, r["asks"] - 120)}, indent=2))
    finally:
        await b.stop(); server.shutdown()
asyncio.run(main())
