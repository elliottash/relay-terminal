import asyncio, base64, json, sys
sys.path.insert(0, '.')
from tests.browser import Browser, shown
from tests.test_remote_pane_state import Harness, state
TOUCH = len(sys.argv) > 3
async def main(height, shot):
    async with Harness() as harness:
        url, _ = await harness.host.open_pairing()
        b = Browser(); await b.start()
        try:
            await b.call("Emulation.setDeviceMetricsOverride", {"width": 1194, "height": height, "deviceScaleFactor": 1, "mobile": TOUCH}); (await b.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5}) if TOUCH else None)
            await b.navigate(url)
            await b.wait_for(shown('screen-inbox'), timeout=40)
            harness.host.pane_state_from_gui(state())
            await b.evaluate("document.querySelectorAll('.pane-row')[0].click()")
            await b.wait_for(shown('screen-thread'))
            await b.wait_for("!!document.querySelector('#pane-view .rp-composer')", timeout=20)
            await asyncio.sleep(1)
            print(await b.evaluate("document.querySelector('.relay-pane').dataset.input + ' ' + document.querySelector('.relay-pane').dataset.device")); print(await b.evaluate("""JSON.stringify([...document.querySelectorAll('#bar, #thread-bar, #pane-view .relay-pane > *')]
               .filter(e => e.getClientRects().length).map(e => { const r = e.getBoundingClientRect();
               return [e.id || e.className, Math.round(r.top), Math.round(r.bottom)]; }))"""))
            data = (await b.call("Page.captureScreenshot", {"format": "png"}))["data"]
            open(shot, 'wb').write(base64.b64decode(data))
        finally:
            await b.stop()
asyncio.run(main(int(sys.argv[1]), sys.argv[2]))
