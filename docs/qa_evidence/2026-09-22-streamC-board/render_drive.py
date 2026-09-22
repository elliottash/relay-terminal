#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #MDX6, measured: the same card bodies through the renderer before and after the fix.

`boardmd-before.js` is `git show 458582b2:app/boardmd.js`; `app/boardmd.js` is the fix. Both are
imported into one real headless Chrome from the same origin and asked to draw the same strings,
and what comes back is the text a reader sees, line by line. Writes `markdown-before-after.md`.

    python3 docs/qa_evidence/2026-09-22-streamC-board/render_drive.py
"""
import asyncio
import json
import sys
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tests.browser import Browser  # noqa: E402

HERE = "/docs/qa_evidence/2026-09-22-streamC-board"

# What a card in this repository says, and what the renderer is being asked to keep.
BODIES = [
    ("a command under a bullet, indented with a tab", "- run this:\n\tmake all"),
    ("a nested bullet, indented with a tab", "- a\n\t- b"),
    ("a tab-indented line of a fence in a list item", "- item\n    ```\n\tb\n    ```"),
    ("a tab-indented line of an indented fence", "    ```sh\n\tmake all\n    ```"),
    ("a Python dunder in a sentence", "the __init__ method and a__b__c"),
    ("a table written straight under a sentence", "Results:\n| a | b |\n| --- | --- |\n| 1 | 2 |"),
    ("bold with underscores, which is not a name", "__also bold__ and **bold**"),
]

DRAW = """(async () => {
  const md = await import(%s);
  const root = md.renderMarkdown(%s, {});
  const BLOCK = /^(p|div|li|ul|ol|pre|h[1-6]|blockquote|table|thead|tbody|tr|th|td|hr)$/;
  let text = '';
  const walk = (node) => { for (const child of node.childNodes) {
    if (child.nodeType === 3) { text += child.nodeValue; continue; }
    const tag = child.tagName.toLowerCase();
    if (tag === 'br') { text += '\\n'; continue; }
    const block = BLOCK.test(tag);
    if (block && text && !text.endsWith('\\n')) text += '\\n';
    walk(child);
    if (block && text && !text.endsWith('\\n')) text += '\\n';
  } };
  walk(root);
  return JSON.stringify({
    lines: text.replace(/\\n+$/, '').split('\\n'),
    lists: root.querySelectorAll('.rb-md-list').length,
    nested: root.querySelectorAll('.rb-md-list .rb-md-list').length,
    pre: [...root.querySelectorAll('.rb-md-pre')].map(e => e.textContent),
    strong: [...root.querySelectorAll('strong')].map(e => e.textContent),
    tables: root.querySelectorAll('.rb-md-table').length,
  });
})()"""


def code(text: str) -> str:
    """One cell of a GitHub table: a code span that survives backticks and bars inside it."""
    return "```` " + json.dumps(text)[1:-1].replace("|", "\\|") + " ````"


def show(page: dict) -> str:
    parts = [" / ".join(repr(line) for line in page["lines"]).replace("|", "\\|")]
    if page["lists"]:
        parts.append(f"{page['lists']} list(s), {page['nested']} nested")
    if page["pre"]:
        parts.append("fence " + " / ".join(repr(x) for x in page["pre"]))
    if page["strong"]:
        parts.append("bold " + " / ".join(repr(x) for x in page["strong"]))
    if page["tables"]:
        parts.append(f"{page['tables']} table")
    return "; ".join(parts)


async def main() -> int:
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=str(ROOT)))
    server.RequestHandlerClass.log_message = lambda *a: None
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_address[1]}"

    browser = Browser()
    await browser.start()
    rows = []
    bad = 0
    try:
        await browser.navigate(f"{origin}{HERE}/")
        await browser.wait_for("document.readyState === 'complete'")
        for what, body in BODIES:
            before = json.loads(await browser.evaluate(
                DRAW % (json.dumps(f"{HERE}/boardmd-before.js"), json.dumps(body))))
            after = json.loads(await browser.evaluate(
                DRAW % (json.dumps("/app/boardmd.js"), json.dumps(body))))
            rows.append((what, body, before, after))
            if before != after:
                bad += 1
    finally:
        await browser.stop()
        server.shutdown()

    out = ["<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->",
           "# Card #MDX6 — the same card bodies, before and after", "",
           "Written by `render_drive.py`: one headless Chrome, both renderers imported from the",
           "same origin, the same strings drawn through each. `before` is `458582b2:app/boardmd.js`.",
           "", "| card body | what it is | before | after |", "|---|---|---|---|"]
    for what, body, before, after in rows:
        out.append(f"| {code(body)} | {what} | {show(before)} | {show(after)} |")
    out += ["", f"{bad} of {len(rows)} bodies drew differently."]
    (Path(__file__).parent / "markdown-before-after.md").write_text("\n".join(out) + "\n")
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
