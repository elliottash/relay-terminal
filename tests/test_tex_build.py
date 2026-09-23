"""`relay_core.tex_build`: the TeX-to-PDF document workspace engine (#WYGY).

The builder tests run the real latexmk on `tests/fixtures/tex_workspace/` (a copy
per test) and are skipped where latexmk or pdflatex is missing. The parser, root,
SyncTeX-output and generation-store tests need no TeX at all. The broken variant
of the fixture is made at run time, never committed.
"""
import gzip
import json
import os
import shutil
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core import tex_build as tb

REPO = Path(__file__).resolve().parent.parent
FIXTURE = REPO / "tests" / "fixtures" / "tex_workspace"
HAVE_TEX = bool(shutil.which("latexmk") and shutil.which("pdflatex"))
HAVE_SYNCTEX = bool(shutil.which("synctex"))
needs_tex = unittest.skipUnless(HAVE_TEX, "latexmk and pdflatex are not installed")


def line_of(path, needle):
    for n, text in enumerate(Path(path).read_text().split("\n"), 1):
        if needle in text:
            return n
    raise AssertionError(f"{needle!r} not in {path}")


# A real pdfTeX log (TeX Live 2023, max_print_line 79), trimmed: the chapter path
# and the citation key are both broken at column 79, and the error is the
# `! ` form, whose file comes only from the log's parenthesised file stack.
WRAPPED_LOG = "\n".join([
    'This is pdfTeX, Version 3.141592653-2.6-1.40.25 (TeX Live 2023/Debian) (preloaded format=pdflatex 2026.9.14)  23 SEP 2026 19:40',
    '(./main.tex',
    'LaTeX2e <2023-11-01> patch level 1',
    '(/usr/share/texlive/texmf-dist/tex/latex/base/article.cls',
    'Document Class: article 2023/05/17 v1.4n Standard LaTeX document class',
    '(/usr/share/texlive/texmf-dist/tex/latex/base/size10.clo',
    'File: size10.clo 2023/05/17 v1.4n Standard LaTeX file (size option)',
    ')',
    '\\c@part=\\count187',
    ')',
    '',
    '(./chapters/a_rather_long_directory_name_for_wrapping_tests/section_with_long_n',
    'ame.tex',
    '',
    'LaTeX Warning: Citation `averyveryverylongcitationkeythatwrapsaroundthelinefors',
    "ureandthensome' on page 1 undefined on input line 2.",
    '',
    '! Undefined control sequence.',
    'l.4 \\undefinedthing',
    '                   ',
    'The control sequence at the end of the top line',
    "of your error message was never \\def'ed. If you have",
    "misspelled it (e.g., `\\hobx'), type `I' and the correct",
    "spelling (e.g., `I\\hbox'). Otherwise just continue,",
    "and I'll forget about whatever was undefined.",
    '',
    ')',
    '',
    "LaTeX Warning: Reference `x' on page 1 undefined on input line 4.",
    '',
    '[1',
    '',
    '{/var/lib/texmf/fonts/map/pdftex/updmap/pdftex.map}] (./main.aux)',
    ' ***********',
    '',
    'LaTeX Warning: There were undefined references.',
    '',
    ' )',
    'Output written on main.pdf (1 page, 14706 bytes).',
    '',
])

# The -file-line-error shapes: a package error with a `(pkg)` continuation, a
# package warning with one, an overfull box with a line range whose content line
# holds an unbalanced parenthesis, and the duplicate `Emergency stop`.
FILE_LINE_LOG = "\n".join([
    'This is pdfTeX, Version 3.141592653-2.6-1.40.25 (TeX Live 2023/Debian) (preloaded format=pdflatex 2026.9.14)  23 SEP 2026 19:40',
    '(./main.tex',
    '(./sections/intro.tex',
    'Package natbib Warning: Citation `nokey\' on page 1 undefined on input line 12.',
    '',
    'Overfull \\hbox (535.14511pt too wide) in paragraph at lines 7--10',
    '[]\\OT1/cmr/m/n/10 An un-de-fined (see the sec-tion',
    ' []',
    '',
    ')',
    './main.tex:20: Package babel Error: Unknown option `klingon\'.',
    '(babel)                Either you misspelled it or the language',
    '(babel)                definition file klingon.ldf was not found.',
    '',
    'See the babel package documentation for explanation.',
    'Type  H <return>  for immediate help.',
    ' ...                                              ',
    '                                                  ',
    'l.20 \\foo(an unbalanced parenthesis in the source',
    '                                                  ',
    'help text',
    '',
    'Package hyperref Warning: Token not allowed in a PDF string (Unicode):',
    '(hyperref)                removing `\\textbf\' on input line 22.',
    '',
    './main.tex:25: Emergency stop.',
    '<*> main.tex',
    '',
    '*** (job aborted, no legal \\end found)',
    '',
])


class LogParserTests(unittest.TestCase):
    def test_wrapped_log_follows_file_stack_and_rejoins_lines(self):
        diags = tb.parse_log(WRAPPED_LOG, build_cwd="/proj")
        chapter = "/proj/chapters/a_rather_long_directory_name_for_wrapping_tests/section_with_long_name.tex"
        got = [(d.severity, d.kind, d.file, d.line) for d in diags]
        self.assertEqual(got, [
            ("warning", "citation", chapter, 2),
            ("error", "latex", chapter, 4),
            ("warning", "reference", "/proj/main.tex", 4),
            ("warning", "reference", "/proj/main.tex", None),
        ])
        self.assertIn("averyveryverylongcitationkeythatwrapsaroundthelineforsureandthensome",
                      diags[0].message)
        self.assertEqual(diags[1].message, "Undefined control sequence.")

    def test_file_line_errors_packages_and_boxes(self):
        diags = tb.parse_log(FILE_LINE_LOG, build_cwd="/proj")
        by_kind = [(d.severity, d.kind, d.file, d.line, d.end_line) for d in diags]
        self.assertEqual(by_kind, [
            ("warning", "citation", "/proj/sections/intro.tex", 12, None),
            ("warning", "box", "/proj/sections/intro.tex", 7, 10),
            ("error", "package", "/proj/main.tex", 20, None),
            ("warning", "package", "/proj/main.tex", 22, None),
        ])
        self.assertIn("definition file klingon.ldf was not found", diags[2].message)
        self.assertTrue(diags[3].message.startswith("hyperref: Token not allowed"))
        self.assertIn("removing `\\textbf'", diags[3].message)

    def test_emergency_stop_alone_is_kept(self):
        diags = tb.parse_log("x\n./main.tex:3: Emergency stop.\n<*> main.tex\n\n", build_cwd="/p")
        self.assertEqual([(d.file, d.line, d.message) for d in diags],
                         [("/p/main.tex", 3, "Emergency stop.")])

    def test_path_map_applies_to_diagnostics(self):
        pm = tb.PathMap("/remote/proj", "/home/me/proj")
        diags = tb.parse_log(FILE_LINE_LOG, build_cwd="/remote/proj", path_map=pm)
        self.assertEqual(diags[0].file, "/home/me/proj/sections/intro.tex")

    def test_unwrap_rejoins_a_utf8_character_split_at_the_break(self):
        text = "LaTeX Warning: Citation `" + "k" * 53 + "é" + "' undefined on input line 3."
        raw = text.encode("utf-8")
        wrapped = raw[:79] + b"\n" + raw[79:] + b"\n"
        self.assertEqual(len(raw[:79]), 79)
        self.assertEqual(tb.unwrap_log(wrapped)[0], text)
        diags = tb.parse_log(wrapped, build_cwd="/p")
        self.assertIn("é", diags[0].message)
        self.assertEqual(diags[0].line, 3)

    def test_unwrapped_log_is_not_rejoined(self):
        a = "x" * 79
        unwrapped = "banner\n" + "y" * 150 + "\n" + a + "\ncontinues here\n"
        self.assertIn(a, tb.unwrap_log(unwrapped))
        wrapped = unwrapped.replace("y" * 150, "y")
        self.assertIn(a + "continues here", tb.unwrap_log(wrapped))
        # a 79-column line followed by the start of a message is not a wrap
        self.assertIn(a, tb.unwrap_log(f"b\n{a}\nLaTeX Warning: next\n"))

    def test_bibtex_log(self):
        blg = "\n".join([
            "Database file #1: refs.bib",
            "I was expecting a `,' or a `}'---line 4 of file refs.bib",
            " :   ",
            " :   publisher = {Addison-Wesley},",
            "(Error may have been on previous line)",
            "I'm skipping whatever remains of this entry",
            'Warning--I didn\'t find a database entry for "ghost"',
            "Warning--empty year in knuth1984",
            "while executing---line 1234 of file plain.bst",
            "I couldn't open database file missing.bib",
        ])
        diags = tb.parse_blg(blg, build_cwd="/proj")
        self.assertEqual([(d.severity, d.kind, d.file, d.line) for d in diags], [
            ("error", "bibtex", "/proj/refs.bib", 4),
            ("warning", "citation", None, None),
            ("warning", "bibtex", None, None),
            ("error", "bibtex", "/proj/missing.bib", None),
        ])

    def test_biber_log_maps_temp_copy_to_bib_file(self):
        blg = "\n".join([
            "[74] bibtex.pm:1519> INFO - Found BibTeX data source 'refs.bib'",
            "[75] Utils.pm:399> ERROR - BibTeX subsystem: /tmp/biber_tmp_5oak/3bbd05_2700332.utf8, "
            'line 4, syntax error: found "year", expected end of entry',
            "[80] Biber.pm:131> WARN - I didn't find a database entry for 'zz' (section 0)",
        ])
        diags = tb.parse_blg(blg, build_cwd="/proj")
        self.assertEqual([(d.severity, d.kind, d.file, d.line) for d in diags], [
            ("error", "biber", "/proj/refs.bib", 4),
            ("warning", "citation", None, None),
        ])
        self.assertTrue(diags[0].message.startswith("refs.bib, line 4, syntax error"))


class SyncTexOutputTests(unittest.TestCase):
    def test_view_records(self):
        out = "\n".join([
            "This is SyncTeX command line utility, version 1.5",
            "SyncTeX result begin",
            "Output:out/main.pdf", "Page:1", "x:208.07", "y:317.58", "h:133.76", "v:319.52",
            "W:343.71", "H:8.85", "before:", "offset:-1", "middle:", "after:",
            "Output:out/main.pdf", "Page:2", "x:10", "y:20", "h:1", "v:2", "W:3", "H:4",
            "SyncTeX result end",
        ])
        recs = tb.parse_synctex_records(out)
        self.assertEqual([r["Page"] for r in recs], ["1", "2"])
        self.assertEqual(recs[0]["W"], "343.71")

    def test_edit_record_keeps_colons_in_paths(self):
        out = ("SyncTeX result begin\nOutput:C:\\p\\main.pdf\nInput:C:\\p\\.\\sec.tex\n"
               "Line:7\nColumn:-1\nOffset:0\nContext:\nSyncTeX result end\n")
        rec = tb.parse_synctex_records(out)[0]
        self.assertEqual(rec["Input"], "C:\\p\\.\\sec.tex")
        self.assertEqual(rec["Line"], "7")

    def test_no_result(self):
        self.assertEqual(tb.parse_synctex_records(
            "This is SyncTeX command line utility, version 1.5\n"
            "SyncTeX Warning: No tag for /x/nope.tex\n"), [])


class PathMapTests(unittest.TestCase):
    def test_round_trip_and_outside_paths(self):
        pm = tb.PathMap("/srv/paper", "/home/me/paper")
        self.assertEqual(pm.to_local("/srv/paper/./sections/a.tex"), "/home/me/paper/sections/a.tex")
        self.assertEqual(pm.to_build("/home/me/paper/sections/a.tex"), "/srv/paper/sections/a.tex")
        self.assertEqual(pm.to_local("/usr/share/texlive/x.sty"), "/usr/share/texlive/x.sty")
        self.assertEqual(pm.to_local("/srv/paperback/a.tex"), "/srv/paperback/a.tex")
        self.assertEqual(pm.relative("/home/me/paper/sections/a.tex"), "sections/a.tex")


class RootAndDependencyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.proj = Path(self.tmp.name, "proj")
        shutil.copytree(FIXTURE, self.proj)

    def tearDown(self):
        self.tmp.cleanup()

    def test_fixture_roots(self):
        main = str(self.proj / "main.tex")
        by_magic = tb.detect_root(str(self.proj / "sections" / "intro.tex"))
        self.assertEqual((by_magic.main_file, by_magic.reason), (main, "magic-comment"))
        by_includer = tb.detect_root(str(self.proj / "sections" / "method.tex"))
        self.assertEqual((by_includer.main_file, by_includer.reason), (main, "includer"))
        by_dir = tb.detect_root(str(self.proj))
        self.assertEqual((by_dir.main_file, by_dir.root_dir), (main, str(self.proj)))
        self.assertEqual(tb.detect_root(main).reason, "documentclass")

    def test_fixture_dependencies_in_document_order(self):
        main = self.proj / "main.tex"
        deps = tb.find_dependencies(str(main))
        self.assertEqual([(d.kind, os.path.relpath(d.path, self.proj), d.exists) for d in deps], [
            ("main", "main.tex", True),
            ("input", "sections/intro.tex", True),
            ("include", "sections/method.tex", True),
            ("bibliography", "refs.bib", True),
        ])
        self.assertEqual(deps[1].line, line_of(main, "\\input{sections/intro}"))
        self.assertTrue(all(d.editable for d in deps))

    def test_latexmkrc_program_comment_and_other_commands(self):
        p = self.proj
        (p / "latexmkrc").write_text("# comment\n@default_files = ('paper.tex');\n$pdf_mode = 5;\n")
        (p / "paper.tex").write_text(
            "% a commented \\input{ghost}\n\\documentclass{article}\n"
            "\\usepackage{import}\n\\begin{document}\n"
            "\\subimport{parts/}{one}\n\\addbibresource[label=x]{lib.bib}\n"
            "\\includegraphics[width=2cm]{fig}\n\\input{missing}\n\\end{document}\n")
        (p / "parts").mkdir()
        (p / "parts" / "one.tex").write_text("\\input{two}\n")  # relative to parts/ under import
        (p / "parts" / "two.tex").write_text("two\n")
        (p / "lib.bib").write_text("")
        (p / "fig.png").write_bytes(b"\x89PNG")
        root = tb.detect_root(str(p))
        self.assertEqual((os.path.basename(root.main_file), root.reason, root.engine),
                         ("paper.tex", "latexmkrc", "xelatex"))
        self.assertEqual(root.project_rc, str(p / "latexmkrc"))
        deps = tb.find_dependencies(root.main_file)
        rel = [(d.kind, os.path.relpath(d.path, p), d.exists) for d in deps]
        self.assertEqual(rel, [
            ("main", "paper.tex", True),
            ("import", "parts/one.tex", True),
            ("input", "parts/two.tex", True),
            ("bibresource", "lib.bib", True),
            ("graphic", "fig.png", True),
            ("input", "missing.tex", False),
        ])
        self.assertFalse(deps[4].editable)
        (p / "main.tex").write_text("% !TeX program = lualatex\n" + (p / "main.tex").read_text())
        self.assertEqual(tb.detect_root(str(p / "main.tex")).engine, "lualatex")

    def test_revision_changes_with_content_only(self):
        a = {str(self.proj / "main.tex"): b"x", str(self.proj / "b.tex"): None}
        r1 = tb.source_revision(a, str(self.proj))
        self.assertEqual(r1, tb.source_revision(dict(reversed(list(a.items()))), str(self.proj)))
        a[str(self.proj / "main.tex")] = b"y"
        self.assertNotEqual(r1, tb.source_revision(a, str(self.proj)))


class FakeRemote(tb.LocalTransport):
    """A 'remote' host that is really this machine, with fetch tampering switches."""

    remote = True

    def __init__(self):
        self.drop: set[str] = set()      # suffixes whose fetch returns None
        self.replace: dict[str, bytes] = {}  # suffix -> bytes to return instead
        self.missing_tools: set[str] = set()
        self.started = threading.Event()

    def run(self, argv, cwd, **kw):
        if argv[0] in self.missing_tools:
            return tb.RunResult(127, f"sh: {argv[0]}: not found", missing=True)
        self.started.set()
        return super().run(argv, cwd, **kw)

    def fetch(self, paths):
        got = super().fetch(paths)
        for p in list(got):
            for suffix in self.drop:
                if p.endswith(suffix):
                    got[p] = None
            for suffix, data in self.replace.items():
                if p.endswith(suffix):
                    got[p] = data
        return got


class DependencyCheckTests(unittest.TestCase):
    def test_local(self):
        tools = tb.check_dependencies(tools=("latexmk", "relay-no-such-tex-tool"))
        self.assertFalse(tools["relay-no-such-tex-tool"].available)
        self.assertEqual(tools["latexmk"].available, bool(shutil.which("latexmk")))
        self.assertIsNone(tools["latexmk"].version)
        self.assertEqual(tb.missing_tools({}, "xelatex"), ["latexmk", "xelatex"])

    @needs_tex
    def test_versions(self):
        tools = tb.check_dependencies(versions=True, tools=("latexmk", "pdflatex"))
        self.assertIn("Latexmk", tools["latexmk"].version)
        self.assertIn("pdfTeX", tools["pdflatex"].version)
        self.assertEqual(tb.missing_tools(tools, "pdf"), [])

    def test_remote_probe(self):
        fake = FakeRemote()
        fake.missing_tools = {"biber"}
        tools = tb.check_dependencies(transport=fake, tools=("biber", "sh"), versions=True)
        self.assertFalse(tools["biber"].available)
        self.assertTrue(tools["sh"].available)


def fake_gz(pages: int) -> bytes:
    body = "SyncTeX Version:1\nInput:1:/p/main.tex\n" + "".join(
        f"{{{i}\n[1,1:0,0:0,0,0\n]\n}}{i}\n" for i in range(1, pages + 1))
    return gzip.compress(body.encode())


def fake_pdf(size: int) -> bytes:
    head, tail = b"%PDF-1.5\n", b"\n%%EOF\n"
    return head + b"x" * (size - len(head) - len(tail)) + tail


def fake_log(pages: int, size: int, name: str = "main.pdf") -> bytes:
    return f"banner\nOutput written on {name} ({pages} pages, {size} bytes).\n".encode()


class GenerationStoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = tb.GenerationStore(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def publish(self, gen_id, pages=2, size=500, **kw):
        args = dict(job="main", pdf=fake_pdf(size), synctex_gz=fake_gz(pages),
                    log_data=fake_log(pages, size))
        args.update(kw)
        return self.store.publish(gen_id, f"rev{gen_id}", **args)

    def test_ids_grow_across_instances(self):
        a = self.store.allocate()
        b = tb.GenerationStore(self.tmp.name).allocate()
        self.assertGreater(b, a)

    def test_older_generation_never_replaces_newer(self):
        self.publish(5)
        with self.assertRaises(tb.GenerationSuperseded):
            self.publish(4)
        with self.assertRaises(tb.GenerationSuperseded):
            self.publish(5)
        self.assertEqual(self.store.current().id, 5)
        self.assertEqual(self.store.current().revision, "rev5")

    def test_parts_must_belong_together(self):
        good = self.publish(1)
        cases = {
            "synctex missing": dict(synctex_gz=None),
            "log from another build": dict(log_data=fake_log(2, 499)),
            "synctex from another build": dict(synctex_gz=fake_gz(3)),
            "truncated pdf": dict(pdf=fake_pdf(500)[:-10]),
        }
        for n, (label, kw) in enumerate(cases.items(), start=2):
            with self.subTest(label), self.assertRaises(tb.GenerationRejected) as cm:
                self.publish(n, **kw)
            self.assertNotIsInstance(cm.exception, tb.GenerationSuperseded)
        cur = self.store.current(verify=True)
        self.assertEqual((cur.id, cur.pdf), (good.id, good.pdf))
        # an XeTeX log names the .xdv, so only the page counts are compared
        self.publish(10, log_data=fake_log(2, 123, "main.xdv"))

    def test_restore_rejects_a_tampered_generation(self):
        gen = self.publish(1)
        own = json.loads(Path(gen.dir, "generation.json").read_text())
        self.assertEqual(own["id"], 1)
        Path(gen.synctex).write_bytes(fake_gz(9))
        self.assertIsNotNone(self.store.current())
        self.assertIsNone(self.store.current(verify=True))

    def test_old_generations_are_pruned(self):
        for i in range(1, 6):
            self.publish(i)
        names = sorted(os.listdir(os.path.join(self.tmp.name, "generations")))
        self.assertEqual(names, ["gen-000003", "gen-000004", "gen-000005"])


class BuilderCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.proj = Path(self.tmp.name, "proj")
        shutil.copytree(FIXTURE, self.proj)
        self.out = Path(self.tmp.name, "out")
        self.states = []
        self.published = []
        self.builders = []

    def tearDown(self):
        for b in self.builders:
            b.close()
        self.tmp.cleanup()

    def builder(self, **kw):
        kw.setdefault("out_dir", str(self.out))
        kw.setdefault("debounce", 0)
        b = tb.TexBuilder(str(kw.pop("root", self.proj)), "main.tex",
                          on_state=lambda s: self.states.append(s),
                          on_published=lambda g: self.published.append(g.id), **kw)
        self.builders.append(b)
        return b

    def build(self, b, **kw):
        b.request_build(**kw)
        return b.wait_idle(timeout=90)

    def edit(self, rel, old, new):
        p = self.proj / rel
        text = p.read_text()
        self.assertIn(old, text)
        p.write_text(text.replace(old, new, 1))


@needs_tex
class BuilderTests(BuilderCase):
    def test_first_build_then_incremental_edit(self):
        b = self.builder()
        argv = b.build_argv()
        self.assertIsInstance(argv, list)
        for flag in ("-norc", "-pdf", "-interaction=nonstopmode", "-file-line-error",
                     "-synctex=1", f"-outdir={self.out / 'work'}"):
            self.assertIn(flag, argv)
        self.assertEqual(argv[-1], "main.tex")
        self.assertEqual(b.status.state, tb.IDLE)

        st = self.build(b)
        self.assertEqual(st.state, tb.LIVE, [d.to_dict() for d in st.diagnostics])
        g1 = st.generation
        self.assertTrue(Path(g1.pdf).read_bytes().startswith(b"%PDF"))
        self.assertTrue(Path(g1.synctex).is_file() and Path(g1.log).is_file())
        self.assertEqual(g1.revision, b.source_revision())
        self.assertEqual(g1.pages, 3)
        self.assertIn(tb.BUILDING, [s.state for s in self.states])
        self.assertEqual([d for d in st.diagnostics if d.severity != "info"], [])
        self.assertEqual(json.loads((self.out / "current.json").read_text())["id"], g1.id)

        self.edit("sections/intro.tex", "A second paragraph",
                  "An edited paragraph cites \\cite{nosuchkey} and a second paragraph")
        b.mark_stale()
        self.assertEqual(b.status.state, tb.STALE)
        st = self.build(b)
        self.assertEqual(st.state, tb.LIVE)
        g2 = st.generation
        self.assertGreater(g2.id, g1.id)
        self.assertNotEqual(g2.revision, g1.revision)
        self.assertEqual(self.published, [g1.id, g2.id])
        seqs = [s.seq for s in self.states]
        self.assertEqual(seqs, sorted(seqs))
        cites = [d for d in st.diagnostics if d.kind == "citation" and d.line]
        self.assertEqual([(d.file, d.line) for d in cites],
                         [(str(self.proj / "sections/intro.tex"),
                           line_of(self.proj / "sections/intro.tex", "nosuchkey"))])
        # an edit and an undo: stale, then live again without a build
        self.edit("main.tex", "\\maketitle", "\\maketitle\\relax")
        b.check_sources()
        self.assertEqual(b.status.state, tb.STALE)
        self.edit("main.tex", "\\maketitle\\relax", "\\maketitle")
        b.check_sources()
        self.assertEqual(b.status.state, tb.LIVE)

    def test_broken_build_fails_and_keeps_last_good_pdf(self):
        b = self.builder()
        good = self.build(b).generation
        good_pdf = Path(good.pdf).read_bytes()
        self.edit("sections/intro.tex", "SYNCMARK", "\\undefinedfixturemacro SYNCMARK")
        bad_line = line_of(self.proj / "sections/intro.tex", "undefinedfixturemacro")

        st = self.build(b)
        self.assertEqual(st.state, tb.FAILED)
        errors = [d for d in st.diagnostics if d.severity == "error"]
        self.assertEqual([(d.file, d.line, d.message) for d in errors],
                         [(str(self.proj / "sections/intro.tex"), bad_line,
                           "Undefined control sequence.")])
        self.assertEqual(st.failed.revision, b.source_revision())
        self.assertTrue(Path(st.failed.log).is_file())
        self.assertEqual(st.generation.id, good.id)
        self.assertEqual(b.store.current(verify=True).id, good.id)
        self.assertEqual(Path(good.pdf).read_bytes(), good_pdf)
        self.assertEqual(self.published, [good.id])
        b.mark_stale()
        self.assertEqual(b.status.state, tb.FAILED)

        self.edit("sections/intro.tex", "\\undefinedfixturemacro ", "")
        st = self.build(b)
        self.assertEqual(st.state, tb.LIVE)
        self.assertGreater(st.generation.id, st.failed.id if st.failed else good.id)
        self.assertIsNone(st.failed)

    def test_superseded_build_is_killed_and_never_publishes(self):
        fake = FakeRemote()
        fake.remote = False
        b = self.builder(transport=fake, timeout=120)
        self.edit("main.tex", "\\maketitle",
                  "\\maketitle\n\\newcount\\spin\\loop\\advance\\spin by 1 "
                  "\\ifnum\\spin<1000000000 \\repeat")
        t0 = time.monotonic()
        b.request_build()
        self.assertTrue(fake.started.wait(10))
        time.sleep(0.5)  # latexmk is now in the TeX loop, minutes from finishing
        slow_id = b.status.building
        self.assertIsNotNone(slow_id)
        self.edit("main.tex", "\\ifnum\\spin<1000000000", "\\ifnum\\spin<10")
        b.request_build()
        st = b.wait_idle(timeout=60)
        self.assertLess(time.monotonic() - t0, 30, "the superseded latexmk was not killed")
        self.assertEqual(st.state, tb.LIVE)
        self.assertGreater(st.generation.id, slow_id)
        self.assertNotIn(slow_id, self.published)
        self.assertEqual(self.published, [st.generation.id])
        self.assertEqual(st.generation.revision, b.source_revision())

    def test_debounce_collapses_saves_and_cancel_settles(self):
        b = self.builder(debounce=0.3)
        for _ in range(4):
            b.request_build()
            time.sleep(0.05)
        st = b.wait_idle(timeout=90)
        self.assertEqual((st.state, len(self.published)), (tb.LIVE, 1))
        b.request_build(debounce=5)
        self.assertEqual(b.status.state, tb.BUILDING)
        b.cancel()
        self.assertEqual(b.wait_idle(timeout=10).state, tb.STALE)
        self.assertEqual(len(self.published), 1)

    @unittest.skipUnless(HAVE_SYNCTEX, "synctex is not installed")
    def test_synctex_round_trip(self):
        b = self.builder()
        self.assertEqual(self.build(b).state, tb.LIVE)
        intro = str(self.proj / "sections/intro.tex")
        target = line_of(intro, "SYNCMARK")
        boxes = b.forward_search(intro, target)
        self.assertTrue(boxes)
        box = boxes[0]
        self.assertEqual(box.page, 1)
        back = b.inverse_search(box.page, box.x, box.y)
        self.assertEqual((back.file, back.relative), (intro, "sections/intro.tex"))
        self.assertLessEqual(abs(back.line - target), 1)
        # \include starts a new page: the method section is on page 2
        method = str(self.proj / "sections/method.tex")
        mbox = b.forward_search(method, line_of(method, "cites Lamport"))[0]
        self.assertEqual(mbox.page, 2)
        self.assertEqual(b.inverse_search(mbox.page, mbox.x, mbox.y).file, method)

    def test_restore_keeps_generation_and_detects_newer_sources(self):
        b = self.builder()
        gen = self.build(b).generation
        b.close()
        again = self.builder()
        self.assertEqual((again.status.state, again.status.generation.id), (tb.LIVE, gen.id))
        again.close()
        self.edit("sections/method.tex", "The method", "The revised method")
        third = self.builder()
        self.assertEqual(third.status.state, tb.STALE)
        st = self.build(third)
        self.assertEqual(st.state, tb.LIVE)
        self.assertGreater(st.generation.id, gen.id)

    def test_dependencies_for_the_editor_group(self):
        b = self.builder()
        rel = [os.path.relpath(d.path, self.proj) for d in b.dependencies() if d.editable]
        self.assertEqual(rel, ["main.tex", "sections/intro.tex", "sections/method.tex", "refs.bib"])


@needs_tex
class FakeRemoteBuildTests(BuilderCase):
    """A 'remote' build: the project lives under remote/, the editor's copy under
    local/, and the builder talks to the remote one only through the transport."""

    def setUp(self):
        super().setUp()
        self.remote_root = Path(self.tmp.name, "remote", "proj")
        shutil.copytree(FIXTURE, self.remote_root)
        self.fake = FakeRemote()
        self.map = tb.PathMap(str(self.remote_root), str(self.proj))

    def remote_builder(self):
        return self.builder(root=self.remote_root, transport=self.fake, path_map=self.map,
                            work_dir=str(Path(self.tmp.name, "remote", "work")))

    def test_generation_integrity(self):
        b = self.remote_builder()
        self.assertNotIn("-r", b.build_argv())
        st = self.build(b)
        self.assertEqual(st.state, tb.LIVE, [d.to_dict() for d in st.diagnostics])
        good = st.generation
        self.assertTrue(good.pdf.startswith(str(self.out)))
        self.assertEqual(tb.check_generation(Path(good.pdf).read_bytes(),
                                             Path(good.synctex).read_bytes(),
                                             Path(good.log).read_bytes()), [])
        if HAVE_SYNCTEX:
            intro = str(self.proj / "sections/intro.tex")
            box = b.forward_search(intro, line_of(intro, "SYNCMARK"))[0]
            self.assertEqual(b.inverse_search(box.page, box.x, box.y).file, intro)

        # the map did not arrive: not published, the previous generation stays
        self.fake.drop = {".synctex.gz"}
        (self.remote_root / "sections/intro.tex").write_text(
            (self.remote_root / "sections/intro.tex").read_text() + "\nMore text.\n")
        st = self.build(b)
        self.assertEqual(st.state, tb.FAILED)
        self.assertIn("SyncTeX map is missing", st.failed.reason)
        self.assertEqual(st.generation.id, good.id)

        # a log left over from the earlier generation beside a new PDF: rejected
        self.fake.drop = set()
        self.fake.replace = {".log": Path(good.log).read_bytes()}
        (self.remote_root / "sections/intro.tex").write_text(
            (self.remote_root / "sections/intro.tex").read_text() + "\n\\newpage Page four.\n")
        st = self.build(b)
        self.assertEqual(st.state, tb.FAILED)
        self.assertIn("bytes", st.failed.reason)
        self.assertEqual(b.store.current(verify=True).id, good.id)

        self.fake.replace = {}
        st = self.build(b)
        self.assertEqual(st.state, tb.LIVE)
        self.assertEqual(self.published, [good.id, st.generation.id])

    def test_remote_diagnostics_map_to_the_editor_paths(self):
        b = self.remote_builder()
        intro = self.remote_root / "sections/intro.tex"
        intro.write_text(intro.read_text().replace("SYNCMARK", "\\nosuchmacro SYNCMARK"))
        st = self.build(b)
        self.assertEqual(st.state, tb.FAILED)
        err = [d for d in st.diagnostics if d.severity == "error"][0]
        self.assertEqual((err.file, err.line),
                         (str(self.proj / "sections/intro.tex"), line_of(intro, "nosuchmacro")))

    def test_remote_needs_a_work_dir(self):
        with self.assertRaises(ValueError):
            tb.TexBuilder(str(self.remote_root), "main.tex", transport=self.fake,
                          out_dir=str(self.out))


if __name__ == "__main__":
    unittest.main()
