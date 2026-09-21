#!/usr/bin/env python3
"""What the shots of the round trip cannot say on their own (card #MDL1 t:a11).

Reads the layout Relay wrote at quit and asserts the three things the restore has to get right:
there is exactly **one** models pane in it, it carries the **tab** it was left on, and it sits
beside a **terminal pane** — which is the pane `RelayWindow::linkRestoredModelsPane` will serve,
since nothing about the served pane is saved.
"""
import json
import sys

state = json.load(open(sys.argv[1], encoding="utf-8"))
found = []


def walk(node, siblings):
    if "split" in node:
        children = node.get("children", [])
        for child in children:
            walk(child, children)
        return
    for key in node:
        if key == "models":
            found.append((node["models"], sorted({k for s in siblings for k in s if k != "models"})))


for window in state.get("windows", []):
    for tab in window.get("tabs", []):
        node = tab.get("node", tab)
        walk(node, [node])

print("models nodes in the saved layout:", len(found))
for node, neighbours in found:
    print("  node:   ", json.dumps(node, sort_keys=True))
    print("  beside: ", neighbours)

assert len(found) == 1, "expected exactly one models pane in the saved layout"
node, neighbours = found[0]
assert node.get("tab") == "available", f'the tab it was left on is not saved: {node.get("tab")!r}'
assert "pane" in neighbours, "the models pane is not beside a terminal pane"
print("OK: one models node, tab=available, beside a terminal pane")
