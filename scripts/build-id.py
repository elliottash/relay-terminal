#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Number a build of the Relay app: the date, the hour, then .01, .02, … for each build that hour.

    build-id.py <directory the binary is in> [--now YYYY-MM-DDTHH]

    2026-09-19.14H.01   the first build between 14:00 and 15:00 on 19 September, local time

Owner, 2026-09-19: "where does relay say what build it is? put that in settings … lets put the date
and then .01, .02, etc", then "do 2026-09-10.14H.01 (where XXH is the 24-H time)". CMake runs this as a POST_BUILD step of the `relay` target, so it runs
exactly when the binary was relinked and at no other time. It keeps the hour's count in
`<dir>/.relay-build-seq.json` and writes the id to `<dir>/relay.build-id`, beside the binary; the
app reads that file when it starts (src/AppPaths.h, relay::buildinfo) and shows it in Options ›
General › Diagnostics, with a notice when the file on disk has moved past the process that is
running. The count is per build directory and per local hour; past 99 it simply grows a digit.
Prints the id.
"""
import argparse
import datetime
import json
import os
import sys
import tempfile

SEQUENCE = ".relay-build-seq.json"
SIDECAR = "relay.build-id"


def hour_of(now: datetime.datetime) -> str:
    return f"{now:%Y-%m-%d}.{now:%H}H"


def next_id(directory: str, hour: str) -> str:
    """`hour` is the bucket, e.g. "2026-09-19.14H"; returns it with the next count appended."""
    path = os.path.join(directory, SEQUENCE)
    count = 0
    try:
        with open(path, encoding="utf-8") as handle:
            state = json.load(handle)
        if state.get("hour") == hour and isinstance(state.get("n"), int) and state["n"] > 0:
            count = state["n"]
    except (OSError, ValueError):
        pass   # no count yet, or a damaged one: the hour starts again at .01
    count += 1
    build = f"{hour}.{count:02d}"
    for name, text in ((SEQUENCE, json.dumps({"hour": hour, "n": count}) + "\n"), (SIDECAR, build + "\n")):
        # Whole-file replace: a Relay starting up never reads half an id.
        fd, temp = tempfile.mkstemp(prefix=name + ".", dir=directory)
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(text)
        os.replace(temp, os.path.join(directory, name))
    return build


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("directory")
    parser.add_argument("--now", help="YYYY-MM-DDTHH (default: this hour, local time)")
    args = parser.parse_args(argv)
    if not os.path.isdir(args.directory):
        print(f"build-id: {args.directory} is not a directory", file=sys.stderr)
        return 1
    try:
        now = datetime.datetime.strptime(args.now, "%Y-%m-%dT%H") if args.now else datetime.datetime.now()
    except ValueError:
        print(f"build-id: --now wants YYYY-MM-DDTHH, not {args.now}", file=sys.stderr)
        return 1
    print(next_id(args.directory, hour_of(now)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
