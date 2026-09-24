"""Small, loss-conscious DOCX bridge for Relay's in-pane editor.

Only simple paragraphs are editable.  The original ZIP entries and every XML element outside
those paragraphs are retained; paragraphs containing drawings, fields, revisions, hyperlinks or
other structures are shown read-only instead of being flattened on save.  The run-splicing idea
comes from Tracelaw's DOCX export, but this module has no Tracelaw dependency.
"""

from __future__ import annotations

import difflib
import hashlib
import html
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
PARAGRAPH = re.compile(rb"<w:p(?:\s[^>]*)?\s*/>|<w:p(?:\s[^>]*)?>.*?</w:p>", re.DOTALL)
RUN = re.compile(rb"<w:r(?:\s[^>]*)?>.*?</w:r>", re.DOTALL)
RPR = re.compile(rb"<w:rPr(?:\s[^>]*)?>.*?</w:rPr>|<w:rPr\s*/>", re.DOTALL)
FLAG = {"bold": "b", "italic": "i", "underline": "u"}
MAX_DOC = 24 * 1024 * 1024
MAX_XML = 48 * 1024 * 1024


def _source(path: Path) -> tuple[bytes, bytes]:
    if path.stat().st_size > MAX_DOC:
        raise ValueError("DOCX is too large to edit here (24 MB limit)")
    original = path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        info = archive.getinfo("word/document.xml")
        if info.file_size > MAX_XML:
            raise ValueError("DOCX document XML is too large to edit here")
        xml = archive.read(info)
    return original, xml


def _run_text(run: ET.Element) -> str:
    parts: list[str] = []
    for child in run:
        if child.tag == W + "t":
            parts.append(child.text or "")
        elif child.tag == W + "tab":
            parts.append("\t")
        elif child.tag in (W + "br", W + "cr"):
            parts.append("\n")
    return "".join(parts)


def _flag(rpr: ET.Element | None, name: str) -> bool:
    node = rpr.find(W + name) if rpr is not None else None
    if node is None:
        return False
    return node.get(W + "val", "1").lower() not in ("0", "false", "off")


def _paragraphs(xml: bytes) -> list[dict]:
    root = ET.fromstring(xml)
    table_positions: dict[ET.Element, tuple[int, int, int]] = {}
    body = root.find(W + "body")
    if body is not None:
        table_id = 0
        for child in body:
            if child.tag != W + "tbl":
                continue
            for row_number, row in enumerate(child.findall(W + "tr")):
                for cell_number, cell in enumerate(row.findall(W + "tc")):
                    for cell_paragraph in cell.iter(W + "p"):
                        table_positions[cell_paragraph] = (table_id, row_number, cell_number)
            table_id += 1
    nodes = list(root.iter(W + "p"))
    spans = list(PARAGRAPH.finditer(xml))
    if len(nodes) != len(spans):
        raise ValueError("DOCX paragraph layout cannot be mapped safely")
    result = []
    for index, (node, span) in enumerate(zip(nodes, spans)):
        raw = span.group()
        children = list(node)
        editable = all(child.tag in (W + "pPr", W + "r") for child in children)
        runs = [child for child in children if child.tag == W + "r"]
        raw_runs = list(RUN.finditer(raw))
        if len(runs) != len(raw_runs):
            editable = False
        out_runs = []
        for n, run in enumerate(runs):
            if any(child.tag not in (W + "rPr", W + "t", W + "tab", W + "br", W + "cr") for child in run):
                editable = False
            rpr = run.find(W + "rPr")
            raw_rpr = RPR.search(raw_runs[n].group()).group().decode("utf-8") if n < len(raw_runs) and RPR.search(raw_runs[n].group()) else ""
            fonts = rpr.find(W + "rFonts") if rpr is not None else None
            size = rpr.find(W + "sz") if rpr is not None else None
            color = rpr.find(W + "color") if rpr is not None else None
            out_runs.append({"text": _run_text(run), "bold": _flag(rpr, "b"),
                             "italic": _flag(rpr, "i"), "underline": _flag(rpr, "u"),
                             "font": fonts.get(W + "ascii", "") if fonts is not None else "",
                             "size": size.get(W + "val", "") if size is not None else "",
                             "color": color.get(W + "val", "") if color is not None else "",
                             "rpr": raw_rpr})
        ppr = node.find(W + "pPr")
        style = ppr.find(W + "pStyle") if ppr is not None else None
        table, row, cell = table_positions.get(node, (-1, -1, -1))
        result.append({"id": index, "editable": editable, "style": style.get(W + "val", "") if style is not None else "",
                       "table": table, "row": row, "cell": cell,
                       "runs": out_runs, "start": span.start(), "end": span.end(), "raw": raw})
    return result


def inspect(path: Path) -> dict:
    original, xml = _source(path)
    paragraphs = _paragraphs(xml)
    return {"sha256": hashlib.sha256(original).hexdigest(),
            "paragraphs": [{key: p[key] for key in ("id", "editable", "style", "table", "row", "cell", "runs")} for p in paragraphs]}


def _with_flags(rpr: str, flags: dict[str, bool]) -> str:
    if not rpr:
        rpr = "<w:rPr></w:rPr>"
    elif rpr.endswith("/>"):
        rpr = "<w:rPr></w:rPr>"
    for key, tag in FLAG.items():
        rpr = re.sub(r"<w:" + tag + r"(?:\s[^>]*)?(?:/>|>.*?</w:" + tag + r">)", "", rpr, flags=re.DOTALL)
        if flags[key]:
            rpr = rpr.replace("</w:rPr>", f"<w:{tag}/></w:rPr>")
    return rpr


def _text_xml(text: str) -> str:
    result = []
    for part in re.split(r"([\t\n])", text):
        if part == "\t":
            result.append("<w:tab/>")
        elif part == "\n":
            result.append("<w:br/>")
        elif part:
            result.append(f'<w:t xml:space="preserve">{html.escape(part, quote=False)}</w:t>')
    return "".join(result)


def _replacement(paragraph: dict, edited: list[dict]) -> bytes:
    old_runs = paragraph["runs"]
    old_text = "".join(run["text"] for run in old_runs)
    new_text = "".join(str(run["text"]) for run in edited)
    if len(new_text) > 100_000:
        raise ValueError("Edited paragraph is too long")
    old_rpr = [r["rpr"] for r in old_runs for _ in r["text"]]
    new_chars = [(char, {name: bool(run.get(name)) for name in FLAG})
                 for run in edited for char in str(run["text"])]
    source_positions: list[int | None] = [None] * len(new_text)
    for match in difflib.SequenceMatcher(a=old_text, b=new_text).get_matching_blocks():
        for offset in range(match.size):
            source_positions[match.b + offset] = match.a + offset
    inherited = None
    for i, source in enumerate(source_positions):
        if source is not None:
            inherited = source
        elif inherited is not None:
            source_positions[i] = inherited
    inherited = None
    for i in range(len(source_positions) - 1, -1, -1):
        if source_positions[i] is not None:
            inherited = source_positions[i]
        elif inherited is not None:
            source_positions[i] = inherited
    chunks: list[tuple[str, dict, str]] = []
    for i, (char, flags) in enumerate(new_chars):
        source = source_positions[i] if source_positions[i] is not None else 0
        rpr = old_rpr[source] if source < len(old_rpr) else (old_runs[0]["rpr"] if old_runs else "")
        if chunks and chunks[-1][1] == flags and chunks[-1][2] == rpr:
            chunks[-1] = (chunks[-1][0] + char, flags, rpr)
        else:
            chunks.append((char, flags, rpr))
    made = "".join(f"<w:r>{_with_flags(rpr, flags)}{_text_xml(text)}</w:r>" for text, flags, rpr in chunks)
    raw = paragraph["raw"]
    matches = list(RUN.finditer(raw))
    if matches:
        return raw[:matches[0].start()] + made.encode() + raw[matches[-1].end():]
    if raw.endswith(b"/>"):
        return raw[:-2] + b">" + made.encode() + b"</w:p>"
    end = raw.rfind(b"</w:p>")
    if end < 0:
        raise ValueError("DOCX paragraph is malformed")
    return raw[:end] + made.encode() + raw[end:]


def save(path: Path, request: dict) -> dict:
    original, xml = _source(path)
    if hashlib.sha256(original).hexdigest() != request.get("sha256"):
        raise ValueError("DOCX changed on disk; reload before saving")
    paragraphs = _paragraphs(xml)
    edits = request.get("edits", [])
    if not isinstance(edits, list):
        raise ValueError("Invalid edit list")
    changes: list[tuple[int, int, bytes]] = []
    seen: set[int] = set()
    for edit in edits:
        index = edit.get("id")
        if not isinstance(index, int) or index < 0 or index >= len(paragraphs) or index in seen:
            raise ValueError("Invalid paragraph ID")
        seen.add(index)
        p = paragraphs[index]
        if not p["editable"]:
            raise ValueError(f"Paragraph {index + 1} contains unsupported Word features")
        runs = edit.get("runs")
        if not isinstance(runs, list) or any(not isinstance(r, dict) or not isinstance(r.get("text"), str) for r in runs):
            raise ValueError("Invalid run data")
        changes.append((p["start"], p["end"], _replacement(p, runs)))
    if not changes:
        return {"sha256": request["sha256"]}
    for start, end, replacement in reversed(sorted(changes)):
        xml = xml[:start] + replacement + xml[end:]
    ET.fromstring(xml)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".relay-docx-", suffix=".docx", dir=path.parent, delete=False) as tmp:
            temporary = Path(tmp.name)
        with zipfile.ZipFile(path) as src, zipfile.ZipFile(temporary, "w") as dst:
            for info in src.infolist():
                dst.writestr(info, xml if info.filename == "word/document.xml" else src.read(info))
        with zipfile.ZipFile(temporary) as check:
            if check.testzip() is not None:
                raise ValueError("Saved DOCX failed ZIP validation")
        os.replace(temporary, path)
        return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    try:
        action, file_name = sys.argv[1:3]
        path = Path(file_name)
        result = inspect(path) if action == "inspect" else save(path, json.load(sys.stdin)) if action == "save" else None
        if result is None:
            raise ValueError("Expected inspect or save")
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError, ET.ParseError, zipfile.BadZipFile) as exc:
        print(json.dumps({"error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
