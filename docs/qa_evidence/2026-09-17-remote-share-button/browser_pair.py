# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive the web client against a pairing URL, for the share-button evidence run.

Used by docs/qa_evidence/2026-09-17-remote-share-button/drive.sh: it stands in for the phone —
scan, compare the code, open the shared pane, take over and type.
"""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tests.browser import SCREENS_SHOWN, Browser, shown


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
        print("paired", flush=True)

        await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await browser.wait_for(shown('terminal-pane'), timeout=30)
        await browser.wait_for("document.querySelectorAll('.screen-row').length > 1", timeout=30)
        text = await browser.evaluate("document.querySelector('.screen-grid').textContent")
        print("phone sees:", " ".join(text.split())[:200], flush=True)

        await browser.evaluate("document.getElementById('term-take').click()")
        await browser.wait_for(shown('term-composer'), timeout=20)
        await browser.evaluate(
            "(() => { const box = document.getElementById('term-line');"
            " box.value = 'printf \"typed from the phone\\\\n\"';"
            " document.getElementById('term-send').click(); return true; })()")
        await asyncio.sleep(4)
        after = await browser.evaluate("document.querySelector('.screen-grid').textContent")
        print("phone sees after typing:", " ".join(after.split())[:240], flush=True)
        problems = [line for line in browser.console if "EXCEPTION" in line]
        print("console problems:", problems, flush=True)
    finally:
        await browser.stop()


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(sys.argv[1]), 240))
