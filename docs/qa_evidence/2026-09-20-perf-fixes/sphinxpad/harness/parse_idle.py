#!/usr/bin/env python3
"""Turn the idle matrix's JSON files into one TSV: one row per run, plus a median summary."""
import glob, json, os, statistics, sys

D = sys.argv[1] if len(sys.argv) > 1 else "/tmp/rx/out"
rows = []
for f in sorted(glob.glob(os.path.join(D, "idle-*.json"))):
    txt = open(f).read()
    dec = json.JSONDecoder()
    objs, i = [], 0
    while i < len(txt):
        while i < len(txt) and txt[i] not in "{":
            i += 1
        if i >= len(txt):
            break
        try:
            o, j = dec.raw_decode(txt, i)
        except ValueError:
            i += 1
            continue
        objs.append(o)
        i = j
    if len(objs) < 2:
        print("skip", f, file=sys.stderr)
        continue
    head, body = objs[0], objs[-1]
    tag = body["tag"]
    if tag == "smoke":
        continue
    build, panes, run = tag.rsplit("-", 2)
    gui = body["rows"][0]
    rows.append(dict(build=build, panes=int(panes), run=int(run), load=float(body["load"]),
                     built=body["panes_built"], cpu=body["total_cpu_pct"], wk=body["total_wakeups_s"],
                     gui_cpu=gui["cpu_pct"], gui_wk=gui["wakeups_s"], gui_pss=gui["pss_kb"],
                     tree_pss=sum(r["pss_kb"] or 0 for r in body["rows"]),
                     rss0=body["mem_at_start"].get("Rss"), nproc=body["nproc"],
                     window_ms=head.get("window"), shell_ms=head.get("shell"),
                     worker_ms=head.get("worker"), wready_ms=head.get("wready")))

cols = ["build", "panes", "run", "built", "load", "cpu", "wk", "gui_cpu", "gui_wk", "gui_pss",
        "tree_pss", "rss0", "nproc", "window_ms", "shell_ms", "worker_ms", "wready_ms"]
print("\t".join(cols))
for r in sorted(rows, key=lambda r: (r["panes"], r["build"], r["run"])):
    print("\t".join(str(r.get(c)) for c in cols))

print("\n== medians of 3 runs ==")
print("build\tpanes\tbuilt\tcpu%\twk/s\tgui_cpu%\tgui_wk/s\tgui_pss_MB\ttree_pss_MB\trss_start_MB\twindow_ms")
for panes in sorted({r["panes"] for r in rows}):
    for build in ("b5", "a5", "b6", "a6"):
        sel = [r for r in rows if r["build"] == build and r["panes"] == panes]
        if not sel:
            continue
        m = lambda k: statistics.median([r[k] for r in sel if r[k] is not None])
        print(f"{build}\t{panes}\t{sel[0]['built']}\t{m('cpu'):.2f}\t{m('wk'):.1f}\t{m('gui_cpu'):.2f}"
              f"\t{m('gui_wk'):.1f}\t{m('gui_pss')/1024:.1f}\t{m('tree_pss')/1024:.1f}"
              f"\t{m('rss0')/1024:.1f}\t{m('window_ms'):.0f}   (n={len(sel)})")
