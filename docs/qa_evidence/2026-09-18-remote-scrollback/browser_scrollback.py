# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive the web client against a real Relay pane and page its scrollback (#W5N2).

Used by docs/qa_evidence/2026-09-18-remote-scrollback/drive.sh: it stands in for the phone —
scan, compare the code, open the shared pane, drag the terminal down, and check that lines which
scrolled off the desktop's screen arrive and are painted like the live screen.
"""
import asyncio
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tests.browser import Browser, shown

HERE = Path(__file__).resolve().parent
ROWS = ".screen-history .screen-row"
# Every row on screen in the order it is painted: the history buffer, then the live block.
COLUMN = ("(() => [...document.querySelectorAll("
          "'.screen-history .screen-row, .screen-grid > .screen-row')]"
          ".map(n => n.textContent.trim()))()")


def seam(rows: list[str]) -> str:
    """Whether the numbered lines in the column are one unbroken run."""
    numbers = [int(text.split("-")[-1]) for text in rows if text.startswith("scrollback-")]
    if not numbers:
        return "no numbered rows on screen"
    missing = [n for n in range(numbers[0], numbers[-1] + 1) if n not in set(numbers)]
    repeated = len(numbers) != len(set(numbers))
    return (f"rows {numbers[0]}..{numbers[-1]} contiguous: {not missing and not repeated}"
            + (f" MISSING {missing[:12]}" if missing else "")
            + (" REPEATED" if repeated else ""))


async def shot(browser: Browser, name: str) -> None:
    result = await browser.call("Page.captureScreenshot", {"format": "png"})
    (HERE / f"phone-{name}.png").write_bytes(base64.b64decode(result["data"]))
    print("shot", name, flush=True)


async def main(url: str) -> None:
    browser = Browser(insecure=True)   # the desktop's development certificate
    await browser.start()
    try:
        await browser.navigate(url)
        code = await browser.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/) "
            "? document.getElementById('pair-code').textContent : ''", timeout=60)
        print("phone shows code", code, flush=True)

        await browser.wait_for(shown('screen-inbox'), timeout=90)
        features = await browser.evaluate(
            "document.getElementById('capability').textContent")
        print("paired as", features, flush=True)

        await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await browser.wait_for(shown('terminal-pane'), timeout=30)
        await browser.wait_for("document.querySelectorAll('.screen-row').length > 1", timeout=30)
        live = await browser.evaluate("document.querySelector('.screen-grid').textContent")
        print("live screen ends:", " ".join(live.split())[-120:], flush=True)
        await shot(browser, "01-live-screen")

        # A page is fetched before it is needed, so there is something above to drag into.
        held = await browser.wait_for(
            f"(() => {{ const t = [...document.querySelectorAll('{ROWS}')]"
            ".map(n => n.textContent.trim()); return t.length ? t : null; })()", timeout=30)
        print("first page:", len(held), "rows, oldest", held[0], "newest", held[-1], flush=True)

        # Drag it down. The poll nudges the scroll to the top the way a finger would.
        deeper = await browser.wait_for(
            f"(() => {{ const w = document.getElementById('screen-wrap');"
            f" const t = [...document.querySelectorAll('{ROWS}')].map(n => n.textContent.trim());"
            " if (t.length >= 200) return t;"
            " w.scrollTop = 0; return null; })()", timeout=60)
        print("after dragging:", len(deeper), "rows, oldest", deeper[0], flush=True)
        numbers = [int(text.split("-")[-1]) for text in deeper if text.startswith("scrollback-")]
        joined = numbers == list(range(numbers[0], numbers[0] + len(numbers)))
        print("pages join with no gap and no repeat:", joined, flush=True)
        print("the whole column:", seam(await browser.evaluate(COLUMN)), flush=True)
        await shot(browser, "02-scrolled-back")

        style = await browser.evaluate(
            f"(() => {{ const row = [...document.querySelectorAll('{ROWS}')]"
            ".find(n => /scrollback-\\d*[05]$/.test(n.textContent.trim()));"
            " const css = getComputedStyle(row.firstElementChild);"
            " return [row.textContent.trim(), css.fontWeight, css.color]; })()")
        print("a history row is painted:", style, flush=True)

        # New output while the reader is up here must move nothing.
        await browser.evaluate(
            "(() => { const w = document.getElementById('screen-wrap');"
            " w.scrollTop = w.scrollHeight - w.clientHeight - 400; })()")
        before = await browser.wait_for(
            "(() => { const w = document.getElementById('screen-wrap');"
            " const was = window.__settled; window.__settled = w.scrollTop;"
            " return was === w.scrollTop ? w.scrollTop : null; })()", timeout=30)
        print("settled at", before, flush=True)

        # Tell the desktop half to print something, and watch what that does to this view.
        settled = HERE / "settled.flag"
        settled.write_text(str(before))
        poke = HERE / "printed.flag"
        for _ in range(600):
            if poke.exists():
                break
            await asyncio.sleep(0.2)
        await asyncio.sleep(2)
        after = await browser.evaluate(
            "document.getElementById('screen-wrap').scrollTop")
        chip = await browser.evaluate(shown('term-new-output'))
        print("scroll before new output:", before, "after:", after,
              "moved:", after != before, flush=True)
        print("the way back to live is offered:", chip, flush=True)
        # The rows that left the live screen must land between the buffer and the live block,
        # not fall down the hole between them.
        column = await browser.wait_for(
            f"(() => {{ const rows = {COLUMN};"
            " const n = rows.filter(t => t.startsWith('scrollback-'))"
            "   .map(t => parseInt(t.split('-')[1], 10));"
            " return n.every((v, i) => i === 0 || v === n[i - 1] + 1) ? rows : null; })()",
            timeout=30)
        print("the whole column:", seam(column), flush=True)
        await shot(browser, "03-new-output-while-scrolled-back")

        await browser.evaluate("document.getElementById('term-new-output').click()")
        await asyncio.sleep(1)
        at_bottom = await browser.evaluate(
            "(() => { const w = document.getElementById('screen-wrap');"
            " return w.scrollHeight - w.scrollTop - w.clientHeight <= 4; })()")
        print("tapping it returns to live:", at_bottom, flush=True)
        print("the whole column:", seam(await browser.evaluate(COLUMN)), flush=True)
        overflow = await browser.evaluate(
            "(() => { const w = document.getElementById('screen-wrap');"
            " return w.scrollWidth - w.clientWidth; })()")
        print("horizontal overflow (0 = the grid fits):", overflow, flush=True)
        await shot(browser, "04-back-at-live")
        problems = [line for line in browser.console if "EXCEPTION" in line]
        print("console problems:", problems, flush=True)
    finally:
        await browser.stop()


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(sys.argv[1]), 300))
