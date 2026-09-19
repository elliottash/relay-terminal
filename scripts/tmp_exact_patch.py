#!/usr/bin/env python3
"""Exact-match replacements in files too large for the edit tool.

Each (path, old, new) must match exactly once, or the script refuses to write
anything and prints which replacement failed. Whitespace-faithful: the strings
are copied from the files themselves.
"""
import sys


def apply(path: str, replacements: list) -> None:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    for old, new in replacements:
        count = text.count(old)
        if count != 1:
            sys.exit(f"{path}: expected 1 match, found {count} for:\n{old[:200]}")
        text = text.replace(old, new)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(f"{path}: {len(replacements)} replacement(s) applied")
