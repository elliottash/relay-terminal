"""Task-plugin manifests, discovery, enablement and activation (#C0Q8)."""
import contextlib
import io
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
import unittest.mock
from pathlib import Path

from relay_core import task_plugins as tp
from relay_core.task_plugins import ManifestError, PluginError, PluginRegistry


def minimal(**extra) -> dict:
    data = {"schema_version": 1, "id": "acme.sql", "version": "0.1.0", "name": "SQL",
            "description": "Run SQL against a local database."}
    data.update(extra)
    return data


def runner_manifest(**extra) -> dict:
    return minimal(runner={"kind": "repl", "command": ["duckdb"]}, requires=[{"program": "duckdb"}], **extra)


def write_pkg(parent: Path, name: str, data, files: dict | None = None) -> Path:
    root = parent / name
    root.mkdir(parents=True, exist_ok=True)
    text = data if isinstance(data, str) else json.dumps(data, indent=2)
    (root / "plugin.json").write_text(text, encoding="utf-8")
    for rel, body in (files or {}).items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
    return root


NO_SYMLINKS = unittest.skipIf(os.name == "nt", "creating symlinks needs privileges on Windows")
SKILL = "---\nname: s\ndescription: a skill\n---\nbody\n"


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name).resolve()
        self.bundled = self.base / "bundled"
        self.global_ = self.base / "config" / "relay" / "plugins"
        self.state = self.base / "config" / "relay" / "plugins.json"
        self.repo = self.base / "repo"
        (self.repo / ".git").mkdir(parents=True)
        self.project = self.repo / ".relay" / "plugins"
        for d in (self.bundled, self.global_, self.project):
            d.mkdir(parents=True, exist_ok=True)
        env = unittest.mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": str(self.base / "config")})
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop("RELAY_PLUGIN_STATE", None)
        self.found = {"duckdb": "/usr/bin/duckdb", "latexmk": "/usr/bin/latexmk", "python3": "/usr/bin/python3"}

    def tearDown(self):
        self.temp.cleanup()

    def which(self, name):
        return self.found.get(name)

    def registry(self) -> PluginRegistry:
        return PluginRegistry(bundled=self.bundled, global_plugins=self.global_, state=self.state, which=self.which)

    def issues(self, data, files=None, name="pkg") -> list:
        root = write_pkg(self.base / "scratch", name, data, files)
        with self.assertRaises(ManifestError) as caught:
            tp.load_manifest(root)
        return caught.exception.issues

    def assertIssue(self, issues, field, *fragments):
        matching = [i for i in issues if i.field == field]
        self.assertTrue(matching, f"no issue for {field}: {[str(i) for i in issues]}")
        text = " ".join(str(i) for i in matching)
        for fragment in fragments:
            self.assertIn(fragment, text)


class SchemaV2(Fixture):
    def test_round_trip(self):
        data = minimal(schema_version=2, router={"language": "python", "prose_fallback": "agent"},
                       console={"program": ["python3", "-i"], "prompt_marks": "osc133",
                                "startup": "startup.py"},
                       completion={"kind": "static", "table": "completion.json"},
                       commands=[{"name": "reset", "description": "Reset state",
                                  "args": [{"name": "force", "required": False}],
                                  "action": {"kind": "program_line", "line": "%reset"},
                                  "when": {"roles": ["console"], "languages": ["python"]}}],
                       requires=[{"program": "python3"}])
        root = write_pkg(self.base, "v2", data, {"startup.py": "pass\n", "completion.json":
                                                    '[{"text":"def","description":"keyword"}]'})
        result = tp.load_manifest(root).to_dict()
        self.assertEqual(2, result["schema_version"])
        self.assertEqual("osc133", result["console"]["prompt_marks"])
        self.assertEqual("static", result["completion"]["kind"])
        self.assertEqual("program_line", result["commands"][0]["action"]["kind"])
        self.assertEqual("agent", result["router"]["prose_fallback"])

    def test_bad_action_and_escaping_startup(self):
        data = minimal(schema_version=2, commands=[{"name": "bad", "description": "Bad",
                           "action": {"kind": "execute", "line": "x"}}],
                       console={"program": ["python3"], "startup": "../escape.py"},
                       requires=[{"program": "python3"}])
        issues = self.issues(data)
        self.assertIssue(issues, "commands[0].action.kind", "not supported")
        self.assertIssue(issues, "console.startup", "..")

    def test_v1_rejects_v2_keys(self):
        self.assertIssue(self.issues(minimal(console={"program": ["python3"]})), "console", "unknown key")


class BundledManifests(unittest.TestCase):
    def test_every_bundled_plugin_is_valid(self):
        found = tp.discover(None, global_plugins=Path(tempfile.gettempdir()) / "no-such-relay-plugins")
        self.assertEqual(found.invalid, [], [str(i) for r in found.invalid for i in r.issues])
        self.assertEqual({"relay.tex", "relay.python", "relay.stata", "relay.shell"}, set(found.plugins))
        for record in found.plugins.values():
            self.assertEqual("bundled", record.origin)
            self.assertTrue(record.digest.startswith("sha256:"))
            for skill in record.manifest.skill_dirs():
                self.assertTrue((skill / "SKILL.md").is_file(), skill)

    def test_document_and_kernel_share_the_schema(self):
        found = tp.discover(None, global_plugins=Path(tempfile.gettempdir()) / "no-such-relay-plugins").plugins
        tex, py, stata = found["relay.tex"].manifest, found["relay.python"].manifest, found["relay.stata"].manifest
        self.assertEqual(("artifact_command", "pdf", "tex"), (tex.runner.kind, tex.preview.adapter, tex.router.language))
        self.assertIn("{file}", tex.runner.command)
        self.assertEqual(("kernel", "python", ("%",)), (py.runner.kind, py.router.language, py.router.prefixes))
        self.assertIn("{connection_file}", py.runner.command)
        self.assertEqual("stata", stata.router.language)
        self.assertEqual({"1:1:1", "2:1"}, set(tex.panes.layouts))
        self.assertIn("variables", py.panes.roles)
        for manifest in (tex, py, stata):
            self.assertTrue(all(t.name.startswith(manifest.tools.group + "_") for t in manifest.tools.items))
            self.assertIn(manifest.runner.command[0], {n for r in manifest.requires for n in r.names})

    def test_manifest_round_trips_through_to_dict(self):
        root = tp.bundled_dir() / "python"
        manifest = tp.load_manifest(root)
        data = manifest.to_dict()
        data.pop("root")
        data.pop("executable")
        self.assertEqual(manifest, tp.parse_manifest(data, root))


class Schema(Fixture):
    def test_minimal_manifest_defaults_to_bash_and_a_console(self):
        manifest = tp.load_manifest(write_pkg(self.base, "m", minimal()))
        self.assertEqual("bash", manifest.router.language)
        self.assertEqual(("console",), manifest.panes.roles)
        self.assertFalse(manifest.executable)

    def test_schema_version_is_required_and_must_be_known(self):
        self.assertIssue(self.issues({k: v for k, v in minimal().items() if k != "schema_version"}),
                         "schema_version", "is missing", "\"schema_version\": 1")
        self.assertIssue(self.issues(minimal(schema_version=3)), "schema_version", "3 is not a schema", "update Relay")
        self.assertIssue(self.issues(minimal(schema_version="1")), "schema_version", "'1'")
        self.assertIssue(self.issues(minimal(schema_version=True)), "schema_version")

    def test_unknown_keys_are_errors_with_suggestions(self):
        issues = self.issues(minimal(requries=[], runner={"kind": "repl", "comand": ["x"]}))
        self.assertIssue(issues, "requries", "did you mean 'requires'")
        self.assertIssue(issues, "runner.comand", "did you mean 'command'")
        self.assertIssue(self.issues(minimal(colour="red")), "colour", "remove it")

    def test_every_issue_names_file_field_and_fix(self):
        issues = self.issues(minimal(id="Bad", version="1", runner={"kind": "rpl", "command": "latexmk -pdf x.tex"}))
        fields = {i.field for i in issues}
        self.assertTrue({"id", "version", "runner.kind", "runner.command"} <= fields, fields)
        for issue in issues:
            self.assertTrue(issue.file.endswith("plugin.json"), issue)
            self.assertTrue(issue.fix, issue)
        self.assertIn("did you mean 'repl'", str(next(i for i in issues if i.field == "runner.kind")))

    def test_bad_ids_are_rejected(self):
        for bad in ("tex", "Relay.Tex", "a..b", "a.b/c", "1a.b", "a.b.c.d.e", "a." + "x" * 40, 7):
            with self.subTest(bad=bad):
                self.assertIssue(self.issues(minimal(id=bad), name=f"id{abs(hash(str(bad)))}"), "id", "namespaced")
        tp.load_manifest(write_pkg(self.base, "ok", minimal(id="me.data-tools")))

    def test_invalid_json_reports_line_and_column(self):
        issues = self.issues('{\n  "schema_version": 1,\n  "id": "a.b",\n}')
        self.assertIn(":4:", issues[0].file)
        self.assertIn("trailing commas", issues[0].fix)

    def test_duplicate_keys_are_rejected(self):
        issues = self.issues('{"schema_version": 1, "id": "a.b", "id": "c.d"}')
        self.assertIssue(issues, "id", "appears twice")

    @NO_SYMLINKS
    def test_manifest_must_not_be_a_symlink(self):
        real = write_pkg(self.base, "real", minimal())
        link = self.base / "linked"
        link.mkdir()
        (link / "plugin.json").symlink_to(real / "plugin.json")
        with self.assertRaises(ManifestError) as caught:
            tp.load_manifest(link)
        self.assertIn("symlink", str(caught.exception))


class Commands(Fixture):
    def test_shell_string_is_rejected_with_an_argv_suggestion(self):
        issues = self.issues(minimal(runner={"kind": "artifact_command", "command": "latexmk -pdf 'my paper.tex'"}))
        self.assertIssue(issues, "runner.command", "shell string", '["latexmk", "-pdf", "my paper.tex"]')

    def test_shell_wrappers_and_env_are_rejected(self):
        for command in (["sh", "-c", "make"], ["bash", "-c", "latexmk x"], ["pwsh", "-Command", "x"],
                        ["cmd", "/c", "x"]):
            with self.subTest(command=command):
                data = minimal(runner={"kind": "repl", "command": command}, requires=[{"program": command[0]}])
                self.assertIssue(self.issues(data, name=command[0]), "runner.command", "shell string")
        data = minimal(runner={"kind": "repl", "command": ["env", "X=1", "python3"]}, requires=[{"program": "env"}])
        self.assertIssue(self.issues(data), "runner.command[0]", "re-dispatches")
        # A shell *as a REPL*, without a command string, is not a shell string.
        tp.load_manifest(write_pkg(self.base, "bash", minimal(runner={"kind": "repl", "command": ["bash", "--norc"]},
                                                               requires=[{"program": "bash"}])))

    def test_program_must_be_a_name_in_requires_or_inside_the_package(self):
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["/usr/bin/duckdb"]})),
                         "runner.command[0]", "outside the package", "\"duckdb\"")
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["duckdb"]})),
                         "runner.command[0]", "not listed in requires", '{"program": "duckdb"}')
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["./../duckdb"]})),
                         "runner.command[0]", "'..'")
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["./bin/missing"]})),
                         "runner.command[0]", "is not a file")
        root = write_pkg(self.base, "own", minimal(runner={"kind": "repl", "command": ["./bin/run", "{workspace}"]}),
                         {"bin/run": "#!/bin/sh\n"})
        self.assertEqual(("./bin/run", "{workspace}"), tp.load_manifest(root).runner.command)
        # An alternative name satisfies the requirement too.
        tp.load_manifest(write_pkg(self.base, "alt", minimal(runner={"kind": "repl", "command": ["stata-mp"]},
                                                              requires=[{"program": "stata", "alternatives": ["stata-mp"]}])))

    def test_placeholders(self):
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["duckdb", "{db}"]},
                                             requires=[{"program": "duckdb"}])), "runner.command[1]", "{db}", "{file}")
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["duckdb", "{connection_file}"]},
                                             requires=[{"program": "duckdb"}])), "runner.command[1]", "kernel")
        self.assertIssue(self.issues(minimal(runner={"kind": "repl", "command": ["{file}"]})),
                         "runner.command[0]", "placeholder")
        root = write_pkg(self.base, "braces", minimal(
            runner={"kind": "repl", "command": ["python3", "-c", "print({{1: 2}})", "{file}"]},
            requires=[{"program": "python3"}]))
        self.assertIn("{file}", tp.load_manifest(root).runner.command)

    def test_env_is_an_allowlist_without_credentials(self):
        for secret in ("OPENAI_API_KEY", "GITHUB_TOKEN", "AWS_SECRET_ACCESS_KEY", "SSH_AUTH_SOCK", "DB_PASSWORD"):
            with self.subTest(secret=secret):
                data = runner_manifest()
                data["runner"]["env"] = ["TEXINPUTS", secret]
                self.assertIssue(self.issues(data, name=secret), "runner.env[1]", "credential")
        data = runner_manifest()
        data["runner"]["env"] = ["BAD-NAME"]
        self.assertIssue(self.issues(data), "runner.env[0]", "not an environment variable")
        data["runner"]["env"] = ["TEXINPUTS", "MPLBACKEND"]
        data["runner"]["cwd"] = "file_dir"
        runner = tp.load_manifest(write_pkg(self.base, "env", data)).runner
        self.assertEqual((("TEXINPUTS", "MPLBACKEND"), "file_dir"), (runner.env, runner.cwd))

    def test_cwd_policy_is_checked(self):
        data = runner_manifest()
        data["runner"]["cwd"] = "/tmp"
        self.assertIssue(self.issues(data), "runner.cwd", "workspace, project_root, file_dir")


class Paths(Fixture):
    def test_skill_paths_cannot_escape_the_package(self):
        cases = {"../other": "'..'", "/etc": "absolute", "~/.ssh": "absolute", "C:/x": "absolute",
                 "skills\\s": "'/'", "skills/../../x": "'..'"}
        for rel, fragment in cases.items():
            with self.subTest(rel=rel):
                self.assertIssue(self.issues(minimal(skills=[rel]), name=f"s{abs(hash(rel))}"), "skills[0]", fragment)

    @NO_SYMLINKS
    def test_symlink_out_of_the_package_is_caught(self):
        outside = self.base / "outside" / "skill"
        outside.mkdir(parents=True)
        (outside / "SKILL.md").write_text(SKILL)
        root = write_pkg(self.base / "scratch", "link", minimal(skills=["skills/s"]))
        (root / "skills").mkdir()
        (root / "skills" / "s").symlink_to(outside)
        with self.assertRaises(ManifestError) as caught:
            tp.load_manifest(root)
        self.assertIn("resolves outside the package", str(caught.exception))

    def test_skill_folder_must_hold_skill_md(self):
        self.assertIssue(self.issues(minimal(skills=["skills/s"]), {"skills/s/README.md": "x"}), "skills[0]", "no SKILL.md")
        self.assertIssue(self.issues(minimal(skills=["skills/none"]), name="none"), "skills[0]", "not a folder")
        root = write_pkg(self.base, "ok", minimal(skills=["skills/s"]), {"skills/s/SKILL.md": SKILL})
        self.assertEqual([root.resolve() / "skills/s"], tp.load_manifest(root).skill_dirs())

    def test_workspace_globs_stay_relative(self):
        self.assertIssue(self.issues(minimal(activation={"files": ["../*.tex"]})), "activation.files[0]", "'..'")
        self.assertIssue(self.issues(minimal(activation={"files": "*.tex"})), "activation.files", '["*.tex"]')
        self.assertIssue(self.issues(minimal(activation={"programs": ["/usr/bin/python3"]})),
                         "activation.programs[0]", "basename")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console", "preview"]},
                                             preview={"adapter": "pdf", "outputs": ["/tmp/*.pdf"]})),
                         "preview.outputs[0]", "absolute")

    @NO_SYMLINKS
    def test_package_digest_refuses_links_out_and_changes_with_content(self):
        root = write_pkg(self.base, "d", minimal())
        first = tp.package_digest(root)
        (root / "notes.txt").write_text("x")
        self.assertNotEqual(first, tp.package_digest(root))
        (root / "leak").symlink_to(self.base / "outside.txt")
        (self.base / "outside.txt").write_text("secret")
        with self.assertRaises(ManifestError):
            tp.package_digest(root)


class RouterToolsPanes(Fixture):
    def test_reserved_prefixes_keep_shell_and_agent_forcing(self):
        for prefix in ("!", "*", "/", "!!"):
            with self.subTest(prefix=prefix):
                self.assertIssue(self.issues(minimal(router={"language": "python", "prefixes": [prefix]}),
                                             name=f"p{ord(prefix[0])}{len(prefix)}"), "router.prefixes[0]", "reserved")
        self.assertIssue(self.issues(minimal(router={"language": "python", "prefixes": ["ab"]})), "router.prefixes[0]")
        self.assertIssue(self.issues(minimal(router={"language": "pyhton"})), "router.language", "did you mean 'python'")

    def test_tools_are_namespaced_and_lazy(self):
        tools = {"group": "sql", "items": [{"name": "query_run", "description": "x"}]}
        self.assertIssue(self.issues(minimal(tools=tools)), "tools.items[0].name", "namespace", "sql_run")
        self.assertIssue(self.issues(minimal(tools={"group": "board", "items": [{"name": "board_x", "description": "x"}]})),
                         "tools.group", "Relay's own")
        self.assertIssue(self.issues(minimal(tools={"group": "sql", "lazy": False,
                                                    "items": [{"name": "sql_run", "description": "x"}]})),
                         "tools.lazy", "on demand")
        self.assertIssue(self.issues(minimal(tools={"group": "sql", "items": []})), "tools.items", "non-empty")
        self.assertIssue(self.issues(minimal(tools={"group": "sql", "items": [{"name": "sql_run"}]})),
                         "tools.items[0].description", "required")
        manifest = tp.load_manifest(write_pkg(self.base, "t", minimal(
            tools={"group": "sql", "items": [{"name": "sql_run", "description": "Run a query."}]})))
        self.assertTrue(manifest.executable)
        self.assertTrue(manifest.tools.lazy)

    def test_panes_and_layouts(self):
        self.assertIssue(self.issues(minimal(panes={"roles": ["editor"]})), "panes.roles", "console")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console", "canvas"]})), "panes.roles[1]", "editor, console")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console", "editor"], "layouts": ["1:1:1"]})),
                         "panes.layouts[0]", "3 columns for 2")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console"], "layouts": ["1-1"]})), "panes.layouts[0]", "':'")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console", "editor"], "layouts": ["2:1"],
                                                    "default_layout": "1:2"})), "panes.default_layout")
        self.assertIssue(self.issues(minimal(preview={"adapter": "pdf", "outputs": ["*.pdf"]})),
                         "panes.roles", "no \"preview\" pane")
        self.assertIssue(self.issues(minimal(panes={"roles": ["console", "preview"]},
                                             preview={"adapter": "docx", "outputs": ["*.docx"]})), "preview.adapter")
        panes = tp.load_manifest(write_pkg(self.base, "p", minimal(panes={"roles": ["editor", "console", "preview"]}))).panes
        self.assertEqual((("1:1:1",), "1:1:1"), (panes.layouts, panes.default_layout))

    def test_requires(self):
        self.assertIssue(self.issues(minimal(requires=[{"program": "/usr/bin/tex"}])), "requires[0].program", "PATH")
        self.assertIssue(self.issues(minimal(requires=[{"program": "tex", "optional": "yes"}])), "requires[0].optional")
        self.assertIssue(self.issues(minimal(requires={"program": "tex"})), "requires", "list of objects")
        req = tp.load_manifest(write_pkg(self.base, "r", minimal(requires=[{"program": "tex", "version_arg": "-v"}]))).requires[0]
        self.assertEqual((("-v",), False), (req.version_arg, req.optional))


class Discovery(Fixture):
    def test_project_wins_over_global_over_bundled_and_shadowing_is_reported(self):
        write_pkg(self.bundled, "sql", minimal(name="bundled"))
        write_pkg(self.global_, "sql", minimal(name="global"))
        write_pkg(self.project, "sql", minimal(name="project"))
        found = tp.discover(self.repo, bundled=self.bundled, global_plugins=self.global_)
        winner = found.plugins["acme.sql"]
        self.assertEqual(("project", "project"), (winner.origin, winner.manifest.name))
        self.assertEqual(["global", "bundled"], [r.origin for r in winner.shadows])
        self.assertEqual({"project"}, {r.shadowed_by.origin for r in found.shadowed})
        rows = {(r["id"], r["origin"]): r for r in self.registry().list(self.repo)}
        self.assertEqual("project", rows[("acme.sql", "bundled")]["shadowed_by"]["origin"])
        self.assertFalse(rows[("acme.sql", "global")]["enabled"])
        self.assertIn("shadowed", rows[("acme.sql", "global")]["enable_reason"])
        self.assertEqual(2, len(rows[("acme.sql", "project")]["shadows"]))
        # Without a workspace there are no project packages: global wins.
        self.assertEqual("global", tp.discover(None, bundled=self.bundled, global_plugins=self.global_)
                         .plugins["acme.sql"].origin)

    def test_nearest_project_folder_wins(self):
        sub = self.repo / "analysis"
        write_pkg(self.project, "sql", minimal(name="root"))
        write_pkg(sub / ".relay" / "plugins", "sql", minimal(name="nearest"))
        found = tp.discover(sub, bundled=self.bundled, global_plugins=self.global_)
        self.assertEqual("nearest", found.plugins["acme.sql"].manifest.name)
        self.assertEqual("root", found.plugins["acme.sql"].shadows[0].manifest.name)

    def test_invalid_packages_are_listed_and_do_not_shadow(self):
        write_pkg(self.bundled, "sql", minimal())
        write_pkg(self.project, "sql", minimal(runner={"kind": "repl", "command": "duckdb -c x"}))
        found = tp.discover(self.repo, bundled=self.bundled, global_plugins=self.global_)
        self.assertEqual("bundled", found.plugins["acme.sql"].origin)
        self.assertEqual(["project"], [r.origin for r in found.invalid])
        row = next(r for r in self.registry().list(self.repo) if not r["valid"])
        self.assertIn("shell string", row["issues"][0]["message"])

    def test_relay_namespace_is_reserved_except_to_override(self):
        write_pkg(self.bundled, "tex", minimal(id="relay.tex"))
        write_pkg(self.project, "tex", minimal(id="relay.tex", name="my tex"))
        write_pkg(self.project, "fake", minimal(id="relay.official"))
        found = tp.discover(self.repo, bundled=self.bundled, global_plugins=self.global_)
        self.assertEqual("my tex", found.plugins["relay.tex"].manifest.name)
        self.assertNotIn("relay.official", found.plugins)
        self.assertIn("reserved", str(found.invalid[0].issues[-1]))

    @NO_SYMLINKS
    def test_symlinked_package_folder_is_refused(self):
        real = write_pkg(self.base / "elsewhere", "sql", runner_manifest())
        (self.project / "sql").symlink_to(real)
        found = tp.discover(self.repo, bundled=self.bundled, global_plugins=self.global_)
        self.assertEqual({}, found.plugins)
        self.assertIn("links outside", str(found.invalid[0].issues[0]))

    def test_folders_without_a_manifest_are_ignored(self):
        (self.project / "notes").mkdir()
        (self.project / "README.md").write_text("x")
        self.assertEqual([], self.registry().list(self.repo))


class Enablement(Fixture):
    def test_defaults(self):
        write_pkg(self.bundled, "b", runner_manifest(id="acme.bundled"))
        write_pkg(self.global_, "g", runner_manifest(id="acme.global"))
        write_pkg(self.project, "p", runner_manifest(id="acme.project"))
        write_pkg(self.project, "ui", minimal(id="acme.ui", skills=["skills/s"]), {"skills/s/SKILL.md": SKILL})
        rows = {r["id"]: r for r in self.registry().list(self.repo)}
        self.assertTrue(rows["acme.bundled"]["enabled"])
        self.assertTrue(rows["acme.global"]["enabled"])
        self.assertFalse(rows["acme.project"]["enabled"])
        self.assertIn("runner command", rows["acme.project"]["enable_reason"])
        self.assertTrue(rows["acme.ui"]["enabled"], rows["acme.ui"]["enable_reason"])
        self.assertFalse(self.state.exists(), "listing must not write state")

    def test_enable_is_persisted_outside_the_repository(self):
        write_pkg(self.project, "p", runner_manifest())
        self.assertTrue(self.registry().enable(self.repo, "acme.sql").enabled)
        self.assertTrue(self.state.is_file())
        self.assertFalse(any(p.name == "plugins.json" for p in self.repo.rglob("*")))
        stored = json.loads(self.state.read_text())
        entry = stored["projects"][str(self.repo)]["acme.sql"]["project"]
        self.assertTrue(entry["enabled"])
        self.assertTrue(entry["digest"].startswith("sha256:"))
        # A fresh registry (another window, a restart) reads it back.
        self.assertTrue(self.registry().enablement(self.repo, "acme.sql").enabled)
        # A subfolder of the project is the same project.
        (self.repo / "src").mkdir()
        self.assertTrue(self.registry().enablement(self.repo / "src", "acme.sql").enabled)

    def test_disable_a_bundled_plugin_for_one_project(self):
        write_pkg(self.bundled, "b", runner_manifest())
        other = self.base / "other"
        (other / ".git").mkdir(parents=True)
        self.assertFalse(self.registry().disable(self.repo, "acme.sql").enabled)
        self.assertFalse(self.registry().enablement(self.repo, "acme.sql").enabled)
        self.assertTrue(self.registry().enablement(other, "acme.sql").enabled)
        self.assertTrue(self.registry().enable(self.repo, "acme.sql").enabled)

    def test_a_cloned_project_cannot_enable_itself(self):
        write_pkg(self.project, "p", runner_manifest())
        # The repository ships a state file claiming it is enabled, in every place Relay might look.
        forged = {"version": 1, "projects": {str(self.repo): {"acme.sql": {"project": {"enabled": True}}}}}
        for path in (self.repo / ".relay" / "plugins.json", self.project / "plugins.json",
                     self.project / "p" / "plugins.json"):
            path.write_text(json.dumps(forged))
        self.assertFalse(self.registry().enablement(self.repo, "acme.sql").enabled)
        with self.assertRaises(PluginError) as caught:
            self.registry().activate(self.repo, "acme.sql")
        self.assertIn("task_plugins enable acme.sql", str(caught.exception))

    def test_enable_does_not_follow_a_copy_to_another_path(self):
        write_pkg(self.project, "p", runner_manifest())
        self.registry().enable(self.repo, "acme.sql")
        clone = self.base / "clone"
        shutil.copytree(self.repo, clone, symlinks=True)
        self.assertFalse(self.registry().enablement(clone, "acme.sql").enabled)

    def test_a_changed_package_needs_enabling_again(self):
        root = write_pkg(self.project, "p", runner_manifest())
        registry = self.registry()
        registry.enable(self.repo, "acme.sql")
        registry.activate(self.repo, "acme.sql")
        data = runner_manifest()
        data["runner"]["command"] = ["duckdb", "-init", "{workspace}"]
        (root / "plugin.json").write_text(json.dumps(data))   # e.g. `git pull`
        ability = registry.enablement(self.repo, "acme.sql")
        self.assertFalse(ability.enabled)
        self.assertIn("changed since it was enabled", ability.reason)
        with self.assertRaises(PluginError):
            registry.activate(self.repo, "acme.sql")
        self.assertTrue(registry.enable(self.repo, "acme.sql").enabled)
        registry.activate(self.repo, "acme.sql")

    def test_unknown_id_suggests(self):
        write_pkg(self.bundled, "b", runner_manifest())
        with self.assertRaises(PluginError) as caught:
            self.registry().enable(self.repo, "acme.sq")
        self.assertIn("Did you mean acme.sql", str(caught.exception))

    def test_corrupt_state_is_reported_not_overwritten(self):
        write_pkg(self.bundled, "b", runner_manifest())
        self.state.parent.mkdir(parents=True, exist_ok=True)
        self.state.write_text("{not json")
        with self.assertRaises(PluginError) as caught:
            self.registry().enable(self.repo, "acme.sql")
        self.assertIn(str(self.state), str(caught.exception))
        self.assertEqual("{not json", self.state.read_text())

    def test_state_path_override(self):
        with unittest.mock.patch.dict(os.environ, {"RELAY_PLUGIN_STATE": str(self.base / "s.json")}):
            self.assertEqual(self.base / "s.json", tp.state_path())
        self.assertEqual(self.base / "config" / "relay" / "plugins.json", tp.state_path())
        self.assertEqual(self.base / "config" / "relay" / "plugins", tp.global_dir())


class Dependencies(Fixture):
    def manifest(self, **extra):
        return tp.load_manifest(write_pkg(self.base, "deps", minimal(**extra)))

    def test_which_only_and_alternatives(self):
        self.found = {"stata-mp": "/usr/local/bin/stata-mp"}
        manifest = self.manifest(requires=[{"program": "stata", "alternatives": ["stata-mp"]},
                                           {"program": "synctex", "optional": True, "install_hint": "TeX Live"}])
        with unittest.mock.patch.object(subprocess, "run", side_effect=AssertionError("launched")):
            first, second = tp.dependency_status(manifest, which=self.which)
        self.assertEqual(("stata-mp", "/usr/local/bin/stata-mp", None, False), (first.found, first.path, first.version, first.missing))
        self.assertTrue(second.missing)
        self.assertIn("synctex", second.error)
        self.assertEqual("TeX Live", second.to_dict()["install_hint"])

    @unittest.skipIf(os.name == "nt", "shell-script fake program")
    def test_probe_runs_only_when_asked(self):
        fake = self.base / "bin" / "faketool"
        fake.parent.mkdir()
        fake.write_text("#!/bin/sh\necho \"faketool 9.1 $1\"\n")
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.found = {"faketool": str(fake), "silent": str(fake)}
        manifest = self.manifest(requires=[{"program": "faketool", "version_arg": "--ver"},
                                           {"program": "silent", "version_arg": []}])
        self.assertIsNone(tp.dependency_status(manifest, which=self.which)[0].version)
        probed, silent = tp.dependency_status(manifest, probe=True, which=self.which)
        self.assertEqual("faketool 9.1 --ver", probed.version)
        self.assertIsNone(silent.version)
        self.assertIn("no version probe", silent.error)

    @unittest.skipIf(os.name == "nt", "shell-script fake program")
    def test_probe_times_out(self):
        fake = self.base / "slow"
        fake.write_text("#!/bin/sh\nsleep 5\n")
        fake.chmod(0o755)
        self.found = {"slow": str(fake)}
        status = tp.dependency_status(self.manifest(requires=[{"program": "slow"}]), probe=True, which=self.which,
                                      timeout=0.2)[0]
        self.assertIsNone(status.version)
        self.assertIn("timed out", status.error)


def install_workspace_plugins(self):
    """A document, a kernel and a REPL plugin in the fixture's bundled folder."""
    write_pkg(self.bundled, "tex", minimal(
        id="acme.tex", activation={"files": ["*.tex", "chapters/*.ltx"]}, router={"language": "tex"},
        runner={"kind": "artifact_command", "command": ["latexmk", "-pdf", "{file}"], "cwd": "file_dir"},
        requires=[{"program": "latexmk"}, {"program": "synctex", "optional": True}],
        tools={"group": "tex", "items": [{"name": "tex_build", "description": "Build."}]},
        skills=["skills/tex"], panes={"roles": ["editor", "console", "preview"], "layouts": ["1:1:1", "2:1"]},
        preview={"adapter": "pdf", "outputs": ["*.pdf"]}), {"skills/tex/SKILL.md": SKILL})
    write_pkg(self.bundled, "py", minimal(
        id="acme.python", activation={"files": ["*.py"], "programs": ["python3*", "ipython"]},
        router={"language": "python", "prefixes": ["%"]},
        runner={"kind": "kernel", "command": ["python3", "-m", "ipykernel_launcher", "-f", "{connection_file}"]},
        requires=[{"program": "python3"}]))
    write_pkg(self.bundled, "st", minimal(
        id="acme.stata", activation={"files": ["*.do"], "programs": ["stata*"]}, router={"language": "stata"},
        runner={"kind": "repl", "command": ["stata"]}, requires=[{"program": "stata"}]))


class Activation(Fixture):
    def setUp(self):
        super().setUp()
        install_workspace_plugins(self)

    def test_activate_returns_the_workspace_state_and_deactivate_restores_bash(self):
        registry = self.registry()
        with unittest.mock.patch.object(subprocess, "run", side_effect=AssertionError("launched")), \
                unittest.mock.patch.object(subprocess, "Popen", side_effect=AssertionError("launched")):
            state = registry.activate(self.repo, "acme.tex", tab="t1")
        self.assertEqual(("active", "acme.tex", "bundled"), (state.state, state.plugin_id, state.origin))
        self.assertEqual({"language": "tex", "prefixes": []}, state.router)
        self.assertEqual(["latexmk", "-pdf", "{file}"], state.runner["command"])
        self.assertEqual("1:1:1", state.panes["default_layout"])
        self.assertEqual("pdf", state.preview["adapter"])
        self.assertEqual("tex", state.tools["group"])
        self.assertTrue(state.skills[0].endswith("skills/tex"))
        self.assertIn("optional synctex not found", state.notes)
        self.assertEqual("acme.tex", registry.active(self.repo, "t1").plugin_id)
        self.assertEqual("inactive", registry.active(self.repo, "t2").state)
        back = registry.deactivate(self.repo, "t1")
        self.assertEqual(("inactive", None), (back.state, back.plugin_id))
        self.assertEqual(tp.DEFAULT_ROUTER, back.router)
        self.assertEqual(["console"], back.panes["roles"])
        self.assertIsNone(back.runner)
        self.assertEqual(["deactivated acme.tex"], back.notes)
        self.assertEqual("inactive", registry.active(self.repo, "t1").state)
        json.dumps(state.to_dict())

    def test_tabs_are_independent_and_switching_replaces(self):
        registry = self.registry()
        registry.activate(self.repo, "acme.tex", tab="a")
        registry.activate(self.repo, "acme.python", tab="b")
        self.assertEqual("acme.tex", registry.active(self.repo, "a").plugin_id)
        switched = registry.activate(self.repo, "acme.python", tab="a")
        self.assertIn("replaced acme.tex", switched.notes)
        self.assertEqual("python", switched.router["language"])

    def test_missing_required_program_blocks_activation(self):
        with self.assertRaises(PluginError) as caught:
            self.registry().activate(self.repo, "acme.stata")
        self.assertIn("stata", str(caught.exception))

    def test_disabling_deactivates(self):
        registry = self.registry()
        registry.activate(self.repo, "acme.tex", tab="a")
        registry.disable(self.repo, "acme.tex")
        self.assertEqual("inactive", registry.active(self.repo, "a").state)
        with self.assertRaises(PluginError) as caught:
            registry.activate(self.repo, "acme.tex", tab="a")
        self.assertIn("disabled for this project", str(caught.exception))


class Selection(Fixture):
    def setUp(self):
        super().setUp()
        install_workspace_plugins(self)

    def test_file_rules(self):
        found = self.registry().select_for(self.repo / "paper.tex", workspace=self.repo)
        self.assertEqual(["acme.tex"], [c.plugin_id for c in found])
        self.assertEqual("file 'paper.tex' matches activation.files '*.tex'", found[0].reason)
        self.assertTrue(found[0].enabled)
        nested = self.registry().select_for(self.repo / "chapters" / "one.ltx", workspace=self.repo)
        self.assertIn("'chapters/one.ltx'", nested[0].reason)
        self.assertEqual([], self.registry().select_for(self.repo / "other" / "one.ltx", workspace=self.repo))
        self.assertEqual([], self.registry().select_for(self.repo / "notes.md", workspace=self.repo))

    def test_program_rules(self):
        for program in ("python3", "/usr/bin/python3.12 -i", ["python3.11", "-q"], "ipython", "C:\\Py\\python3.exe"):
            with self.subTest(program=program):
                found = self.registry().select_for(foreground_program=program)
                self.assertEqual(["acme.python"], [c.plugin_id for c in found])
                self.assertIn("foreground program", found[0].reason)
        self.assertEqual([], self.registry().select_for(foreground_program="vim paper.tex"))
        self.assertEqual([], self.registry().select_for(foreground_program=""))
        stata = self.registry().select_for(foreground_program="stata-mp")
        self.assertEqual(("acme.stata", ("stata",)), (stata[0].plugin_id, stata[0].missing))

    def test_file_and_program_reasons_combine(self):
        found = self.registry().select_for(self.repo / "a.py", "python3", workspace=self.repo)
        self.assertEqual(2, found[0].reason.count("matches"))

    def test_disabled_project_candidates_are_listed_after_enabled_ones(self):
        write_pkg(self.project, "sql", minimal(id="acme.sqltex", activation={"files": ["*.tex"]},
                                              runner={"kind": "repl", "command": ["latexmk"]},
                                              requires=[{"program": "latexmk"}]))
        found = self.registry().select_for(self.repo / "paper.tex")   # workspace from the file's folder
        self.assertEqual(["acme.tex", "acme.sqltex"], [c.plugin_id for c in found])
        self.assertFalse(found[1].enabled)
        self.assertIn("enable it", found[1].enable_reason)


class Cli(Fixture):
    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = tp.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_validate(self):
        good = write_pkg(self.base, "good", minimal())
        bad = write_pkg(self.base, "bad", minimal(schema_version=3))
        self.assertEqual(0, self.run_cli("validate", str(good))[0])
        code, _, err = self.run_cli("validate", str(bad))
        self.assertEqual(1, code)
        self.assertIn("schema_version", err)
        self.assertIn("Fix:", err)

    def test_list_enable_and_select(self):
        write_pkg(self.global_, "sql", runner_manifest(activation={"files": ["*.sql"]}))
        write_pkg(self.project, "proj", runner_manifest(id="acme.proj"))
        code, out, _ = self.run_cli("list", "--workspace", str(self.repo), "--json")
        self.assertEqual(0, code)
        rows = {r["id"]: r for r in json.loads(out)}
        self.assertIn("relay.tex", rows)       # the real bundled plugins
        self.assertFalse(rows["acme.proj"]["enabled"])
        code, out, _ = self.run_cli("enable", "acme.proj", "--workspace", str(self.repo))
        self.assertEqual(0, code, out)
        self.assertIn("enabled", out)
        self.assertTrue(self.state.is_file())
        code, out, _ = self.run_cli("select", "--path", str(self.repo / "q.sql"), "--workspace", str(self.repo))
        self.assertIn("acme.sql", out)
        code, _, err = self.run_cli("enable", "nobody.here", "--workspace", str(self.repo))
        self.assertEqual(2, code)
        self.assertIn("no task plugin", err)


if __name__ == "__main__":
    unittest.main()
