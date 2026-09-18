---
id: WQFS
type: work
status: ready
labels: [feature]
assignee: agent
rank: zzzz11a
created: '2026-09-18'
acceptance: a PDF opens in a Relay pane rather than an external viewer
source: 'issues/feature_intake.txt, 2026-09-18: "add in-app PDF rendering"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# In-app PDF rendering

## Request
add in-app PDF rendering

## Plan
**Goal** — Opening a PDF renders it inside the app (a pane of extracted, wrapped text) instead of handing it to an external viewer. Meets the acceptance: "a PDF opens in a Relay pane rather than an external viewer."

**Findings** — This repo is the toy shell: `src/complete.py` (`candidates`, `complete`) plus `tests/test_complete.py` are the entire source. There is no file-open command, no pane/widget code, and nothing that shells out to `xdg-open`/`open` today, so the renderer is built from scratch; the only existing convention to keep is one small module in `src/` with a matching `tests/test_*.py` (as `#ZW95` did).

**Steps**
1. Add `src/pdf_pane.py` with `render(path, width=80, page=None) -> list[str]`: a header line (`<filename> — page i/n`), then that page's text wrapped to `width`, paginated by `page` (1-based; `None` means all pages joined with a page-break header). Text extraction via `pypdf.PdfReader` (new dependency — see Risks).
2. Add `python -m src.pdf_pane FILE [PAGE]` (`main()` + `if __name__ == "__main__"`) so the rendered pane prints into the terminal pane that runs it; non-PDF input exits non-zero with a clear message.
3. Keep rendering strictly in-process: no `subprocess`, `os.system`, `os.startfile`, or viewer launch anywhere in the module.
4. Add `tests/test_pdf_pane.py`: build a small two-page fixture with `pypdf`'s writer in `setUp`, then assert (a) the header names the file and page, (b) body lines are the page's text and never exceed `width`, (c) `page=2` yields page 2 only, (d) a `unittest.mock.patch` on `subprocess.Popen` proves `render` never launches anything, (e) a non-PDF path raises/errs cleanly.

**Risks** — Scanned/image-only PDFs have no extractable text; the pane then shows the header plus a "no extractable text on this page" line rather than failing. Adding `pypdf` is the one decision for the owner: recommended (small, pure-Python); the stdlib-only alternative is a hand-rolled parser that only handles uncompressed text streams. Note this repo has no real pane widget, so "pane" here is the module's rendered output printed by the CLI; wiring it into Relay's actual pane UI lives outside this repo.

**Verify** — `python -m unittest discover tests` (all of `test_complete.py` plus the new file) and, by hand, `python -m src.pdf_pane <some.pdf>` printing the rendered pane in the terminal; record output under `docs/qa_evidence/` per house convention.
