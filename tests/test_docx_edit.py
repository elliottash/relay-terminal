"""DOCX edits keep the package and refuse unmappable or stale changes."""

import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

from relay_core import docx_edit


DOCUMENT = b'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body>
<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:rPr><w:b/><w:color w:val="FF0000"/></w:rPr><w:t>Opening</w:t></w:r></w:p>
<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell value</w:t></w:r></w:p></w:tc></w:tr></w:tbl>
<w:p><w:hyperlink r:id="rId7"><w:r><w:t>Linked text</w:t></w:r></w:hyperlink></w:p>
<w:p/>
<w:sectPr/>
</w:body></w:document>'''


class DocxEditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sample.docx"
        with zipfile.ZipFile(self.path, "w") as archive:
            archive.writestr("[Content_Types].xml", b"types")
            archive.writestr("word/document.xml", DOCUMENT)
            archive.writestr("word/media/image1.png", b"image bytes")
            archive.writestr("word/footnotes.xml", b"footnotes bytes")

    def test_roundtrip_preserves_package_and_run_properties(self):
        opened = docx_edit.inspect(self.path)
        self.assertEqual([p["editable"] for p in opened["paragraphs"]], [True, True, False, True])
        saved = docx_edit.save(self.path, {"sha256": opened["sha256"], "edits": [
            {"id": 0, "runs": [{"text": "Opening revised", "bold": True, "italic": True, "underline": False}]},
            {"id": 1, "runs": [{"text": "Changed cell", "bold": False, "italic": False, "underline": False}]},
            {"id": 3, "runs": [{"text": "New end", "bold": False, "italic": False, "underline": False}]},
        ]})
        with zipfile.ZipFile(self.path) as archive:
            xml = archive.read("word/document.xml")
            self.assertIn(b"<w:color w:val=\"FF0000\"/>", xml)
            self.assertIn(b"<w:i/>", xml)
            self.assertIn(b"<w:t xml:space=\"preserve\">Changed cell</w:t>", xml)
            self.assertIn(b"<w:hyperlink r:id=\"rId7\">", xml)
            self.assertEqual(archive.read("word/media/image1.png"), b"image bytes")
            self.assertEqual(archive.read("word/footnotes.xml"), b"footnotes bytes")
        self.assertEqual(saved["sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        reopened = docx_edit.inspect(self.path)
        self.assertEqual("".join(r["text"] for r in reopened["paragraphs"][0]["runs"]), "Opening revised")
        self.assertEqual("".join(r["text"] for r in reopened["paragraphs"][3]["runs"]), "New end")

    def test_stale_file_and_complex_paragraph_refused_without_write(self):
        opened = docx_edit.inspect(self.path)
        before = self.path.read_bytes()
        with self.assertRaisesRegex(ValueError, "unsupported Word features"):
            docx_edit.save(self.path, {"sha256": opened["sha256"], "edits": [
                {"id": 2, "runs": [{"text": "replace link"}]},
            ]})
        self.assertEqual(self.path.read_bytes(), before)
        with zipfile.ZipFile(self.path, "a") as archive:
            archive.writestr("custom.xml", b"external change")
        changed = self.path.read_bytes()
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            docx_edit.save(self.path, {"sha256": opened["sha256"], "edits": [
                {"id": 0, "runs": [{"text": "stale"}]},
            ]})
        self.assertEqual(self.path.read_bytes(), changed)


if __name__ == "__main__":
    unittest.main()
