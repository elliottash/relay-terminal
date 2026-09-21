#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Stage the human-QA scenario for card #7BM4: a small project in which the problems the card
was written to solve are actually happening, so a tester is *in* the situation instead of being
told about it (owner, 2026-09-20: "a user is put into a test case that simulates the problem the
issue was designed to address"; "design for an AI to try simulating first").

    python3 stage.py                 # stage ~/relay-qa/7bm4-orders and say how to open it
    python3 stage.py --dir /tmp/x    # somewhere else
    python3 stage.py --fresh         # wipe a previous staging first

What it builds — `orders`, a tiny C++/Python project with a Switchboard:

1. **"The agent says it is done."**  A card in Needs verification whose thread says "fixed, all
   tests pass", and whose `## Tests` lists one test that passes, one that **fails**, one that was
   **deleted**, and a Python file that has **never run**.
2. **"CI goes red about once a week."**  Ten days of seeded run history from two machines in
   which `inventory_sync` passes and fails at the same commit, and `report_export` went from
   0.3 s to several seconds.  Both tests really behave that way when run.
3. **"The build got slow."**  One translation unit that costs most of the compile time.

Nothing here talks to a model or the network.  `scenario.json` beside this file is the same
scenario in a form an agent can drive: each step has an actor, an action and an expectation.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import shutil
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "backend"))
from relay_core import board as B            # noqa: E402
from relay_core import test_history as H     # noqa: E402
from relay_core import test_probe as P       # noqa: E402

FILES = {
    "CMakeLists.txt": """cmake_minimum_required(VERSION 3.16)
project(orders CXX)
set(CMAKE_CXX_STANDARD 17)
add_library(orders src/totals.cpp src/inventory.cpp src/report.cpp)
target_include_directories(orders PUBLIC src)
add_executable(totals_test tests/totals_test.cpp)
target_link_libraries(totals_test orders)
add_executable(totals_large_order_test tests/totals_large_order_test.cpp)
target_link_libraries(totals_large_order_test orders)
enable_testing()
add_test(NAME totals COMMAND totals_test)
add_test(NAME totals_large_order COMMAND totals_large_order_test)
add_test(NAME inventory_sync COMMAND sh ${CMAKE_SOURCE_DIR}/tests/inventory_sync.sh)
add_test(NAME report_export COMMAND sh ${CMAKE_SOURCE_DIR}/tests/report_export.sh)
# `rounding` was removed here last week; the card below still lists it.
""",
    "src/orders.h": "#pragma once\nint order_total(int items, int unit_price_cents);\nint stock_after(int stock, int sold);\nint report_lines(const char *text);\n",
    # The bug the card is about: the running total is a short, so 150 x 100 wraps.
    "src/totals.cpp": '#include "orders.h"\nint order_total(int items, int unit_price_cents) {\n    short total = 0;                       // wraps above 32767 cents: the bug on the card\n    for (int i = 0; i < items; ++i) total = short(total + unit_price_cents);\n    return total;\n}\n',
    "src/inventory.cpp": '#include "orders.h"\nint stock_after(int stock, int sold) { return stock - sold; }\n',
    # The slow translation unit: a header-heavy file, which is what scenario 3 should point at.
    "src/report.cpp": '#include "orders.h"\n#include <algorithm>\n#include <functional>\n#include <iostream>\n#include <map>\n#include <regex>\n#include <sstream>\n#include <unordered_map>\n#include <vector>\nint report_lines(const char *text) {\n    static const std::regex line("[^\\\\n]+"), word("\\\\w+"), number("[0-9]+(\\\\.[0-9]+)?");\n    std::string s(text); std::map<std::string, std::vector<int>> seen; std::unordered_map<int, std::function<int(int)>> f;\n    int n = 0;\n    for (std::sregex_iterator it(s.begin(), s.end(), line), end; it != end; ++it) {\n        std::string l = it->str(); ++n;\n        for (std::sregex_iterator w(l.begin(), l.end(), word); w != end; ++w) seen[w->str()].push_back(n);\n        if (std::regex_search(l, number)) f[n] = [n](int x) { return x + n; };\n    }\n    std::ostringstream o; o << seen.size() << f.size(); return n + int(o.str().size()) * 0;\n}\n',
    "tests/totals_test.cpp": '#include "orders.h"\n#include <cstdio>\nint main() { int t = order_total(3, 250); if (t != 750) { std::printf("expected 750 got %d\\n", t); return 1; } return 0; }\n',
    "tests/totals_large_order_test.cpp": '#include "orders.h"\n#include <cstdio>\nint main() {\n    int t = order_total(150, 10000);   // 150 items at 100.00\n    if (t != 1500000) { std::printf("FAIL: order of 150 items: expected 1500000 cents, got %d\\n", t); return 1; }\n    return 0;\n}\n',
    # Really flaky (about one run in three) and really slow, so a live run agrees with the history.
    "tests/inventory_sync.sh": '#!/bin/sh\n# Talks to a "warehouse" that answers late one time in three.\nn=$(( $(date +%s%N) / 1000 % 3 ))\nif [ "$n" -eq 0 ]; then echo "FAIL: warehouse did not answer within 200 ms"; exit 1; fi\nexit 0\n',
    "tests/report_export.sh": '#!/bin/sh\n# Exports every order since January; it used to export one week.\nsleep 2.6\nexit 0\n',
    "tests/test_invoice.py": 'import unittest\n\n\nclass InvoiceTests(unittest.TestCase):\n    def test_total_matches_the_order(self):\n        self.assertEqual(150 * 10000, 1500000)\n\n    def test_large_order_has_one_line_per_item(self):\n        self.assertEqual(len(range(150)), 150)\n\n    def test_currency_is_printed_with_two_decimals(self):\n        self.assertEqual(f"{1500000 / 100:.2f}", "15000.00")\n',
    ".gitignore": "build/\n",
    "README.md": "# orders\n\nA staged project for human QA of Relay card #7BM4. Safe to delete.\n",
}

CARDS = [
    dict(key="done", category="bugs", status="needs-verification", title="Order totals are wrong above 100 items",
         request="a customer ordered 150 units and the invoice total was negative. fix it and make sure it cannot come back.",
         labels=["bug", "totals"], assignee="agent",
         tests=["- `ctest -R totals` — tests/totals_test.cpp",
                "- `ctest -R totals_large_order` — tests/totals_large_order_test.cpp",
                "- `ctest -R rounding` — tests/rounding_test.cpp",
                "- `tests/test_invoice.py`"],
         thread=("agent", "progress", "Fixed the overflow in `src/totals.cpp` and added `totals_large_order`. All tests pass; moving to Needs verification.")),
    dict(key="flaky", category="bugs", status="inbox", title="CI goes red about once a week and nobody knows which test",
         request="every few days the run is red, someone reruns it and it is green. which test is it, and since when?",
         labels=["bug", "tests"]),
    dict(key="slow", category="features", status="inbox", title="The build got slow last month: where does the time go?",
         request="a clean build used to be quick. before anyone guesses, measure it.", labels=["feature", "build"]),
]


def run(cmd, cwd, **kw):
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True, **kw)


def stage(target: Path) -> dict:
    for rel, text in FILES.items():
        path = target / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    env = dict(os.environ, GIT_AUTHOR_NAME="QA", GIT_AUTHOR_EMAIL="qa@example.invalid",
               GIT_COMMITTER_NAME="QA", GIT_COMMITTER_EMAIL="qa@example.invalid")
    run(["git", "init", "-q", "-b", "main"], target)
    run(["git", "add", "-A"], target)
    run(["git", "commit", "-q", "-m", "orders: totals, inventory, report"], target, env=env)
    head = run(["git", "rev-parse", "HEAD"], target).stdout.strip()

    board = B.Board(target / B.BOARD_FOLDERS[0], target)
    B.scaffold(board)
    tabs = {t["id"]: t.get("folder", t["id"]) for t in board.config().get("tabs", []) if t.get("folder")}
    ids = {}
    for rank, spec in zip("cfk", CARDS):
        fields = {"labels": spec["labels"]}
        if spec.get("assignee"):
            fields["assignee"] = spec["assignee"]
        card = B.new_card("work", spec["title"], spec["status"], created=datetime.now().strftime("%Y-%m-%d"),
                          rank=rank, request=spec["request"], **fields)
        folder = tabs.get(spec["category"]) or next(iter(tabs.values()))
        path = B.write_new_card(board, card, folder)
        ids[spec["key"]] = card.id
        text = path.read_text(encoding="utf-8")
        if spec.get("tests"):
            text = text.rstrip("\n") + "\n\n## Tests\n" + "\n".join(spec["tests"]) + "\n"
            text = text.replace("commits: []", f"commits: [{head[:12]}]", 1)
        path.write_text(text, encoding="utf-8")
        if spec.get("thread"):
            author, kind, body = spec["thread"]
            stamp = (datetime.now(timezone.utc) - timedelta(hours=2)).strftime("%Y%m%dT%H%M%SZ")
            (board.root / B.THREADS_FOLDER / f"{card.id}.md").write_text(
                f"<!-- relay:entry {stamp}-a1 author={author} kind={kind} -->\n{body}\n", encoding="utf-8")

    run(["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], target)
    run(["cmake", "--build", "build", "-j4"], target)

    # Ten days of history from two machines.  The hashes are the real ones, so nothing reads as
    # "edited"; the commits are invented except the last, which is HEAD.
    hashes = {t["id"]: t.get("source_hash", "") for t in P.discover(target)["tests"]}
    rng = random.Random(7)
    commits = ["3f2a9c1e", "8b17d04a", "c55e2f90", head[:8]]
    now = datetime.now(timezone.utc)
    rows = []
    for i in range(20):                               # oldest first
        ts = now - timedelta(hours=12 * (20 - i))
        host = "desktop" if i % 2 == 0 else "laptop"
        commit = commits[min(i // 5, 3)]
        def add(name, result, seconds, message=""):
            rows.append(H.Execution(ts=ts.strftime("%Y-%m-%dT%H:%M:%SZ"), id=f"ctest:{name}", result=result,
                                    duration=seconds, runner="ctest", run_id=f"seed-{i:02d}", commit=commit,
                                    host=host, message=message, source_hash=hashes.get(f"ctest:{name}", "")))
        add("totals", "pass", 0.004 + rng.random() * 0.002)
        flake = i in (2, 6, 9, 13, 16, 18)            # a pass and a fail at the same commit, again and again
        add("inventory_sync", "fail" if flake else "pass", 0.03 + rng.random() * 0.01,
            "FAIL: warehouse did not answer within 200 ms" if flake else "")
        add("report_export", "pass", (0.3 + rng.random() * 0.05) if i < 12 else (2.4 + rng.random() * 1.7))
        if i >= 15:                                   # added with the "fix"; it has never passed
            add("totals_large_order", "fail", 0.004,
                "FAIL: order of 150 items: expected 1500000 cents, got -7296")
    H.append(rows, H.default_path(target, board.root))
    return {"dir": str(target), "cards": ids, "head": head}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dir", default=str(Path.home() / "relay-qa" / "7bm4-orders"))
    ap.add_argument("--fresh", action="store_true")
    args = ap.parse_args()
    target = Path(args.dir).expanduser()
    if target.exists():
        if not args.fresh:
            print(f"{target} exists; pass --fresh to stage it again", file=sys.stderr)
            return 2
        shutil.rmtree(target)
    target.mkdir(parents=True)
    out = stage(target)
    (target / "STAGED.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out, indent=2))
    print(f"\nOpen it in Relay:  <relay binary> --workspace {target}   then Ctrl+Shift+S")
    return 0


if __name__ == "__main__":
    sys.exit(main())
