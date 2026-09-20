# SPDX-License-Identifier: AGPL-3.0-or-later
"""The two shapes a tool's output takes on the worker→GUI wire (protocol 23.10, card #PPR4).

`tool_output` carries the text a running tool has printed, and `tool_result` carries the whole of
it again in `result.output`. Between them they were 81 % of the bytes of a tool-heavy turn — about
66 KB per call — and in Relay's default configuration the GUI reads neither: the call's own line
counts the newlines (#TK9C) and the fold fetches the real text with `tool_output_get` when it is
opened. The profile (`docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md`, finding 2)
put `QJsonDocument::fromJson` at 7.1 % of GUI cycles because of it.

So the GUI may ask, with `stream_tool_output: false`, for the counts instead of the text. Nothing
is *lost*: the worker keeps every result for the last 50 turns and `tool_output_get` still answers
with the full text, which is where every surface that shows output already gets it from. The
default is true, so a GUI that never sends the option, and a phone watching through one that does
not know about it, see exactly what they saw before.
"""

# The fields of a `tool_result`'s `result` that are the call's output rather than its verdict.
# Each becomes `<field>_bytes` / `<field>_lines`, so a consumer can still say how much there was
# and where to ask for it. Everything else — `exit_code`, `error`, `job_id`, `still_running`,
# `truncated`, `omitted_bytes`, the write counts — is short and is sent unchanged, because that is
# what the concise line (§ 23.2) and the wrong-mode hint read.
TEXT_FIELDS = ("output", "content", "screen")


def validate(value) -> bool:
    """`stream_tool_output` from configure / set_agent_options."""
    if type(value) is not bool:
        raise ValueError("stream_tool_output must be a boolean.")
    return value


def _counts(text: str) -> tuple[int, int]:
    return text.count("\n"), len(text.encode("utf-8"))


def counted(event: dict) -> dict:
    """`event` with the text of a tool's output replaced by its counts, or `event` unchanged.

    Only the two streaming events are touched. The stored reply to `tool_output_get` is also
    called `tool_output` (the name collision noted in section 5) but carries `stored: true` and no
    `text`, and it is the one surface that must keep the full output, so it is left alone.
    """
    kind = event.get("event")
    if kind == "tool_output":
        if event.get("stored") or not isinstance(event.get("text"), str):
            return event
        lines, size = _counts(event["text"])
        out = {key: value for key, value in event.items() if key != "text"}
        # `partial` is what the live row needs beside the count: a chunk that does not end on a
        # newline leaves a line open, and the row counts it as one (#TK9C).
        out["lines"] = lines
        out["bytes"] = size
        out["partial"] = bool(event["text"]) and not event["text"].endswith("\n")
        out["counted"] = True
        return out
    if kind != "tool_result" or not isinstance(event.get("result"), dict):
        return event
    result = event["result"]
    trimmed = {}
    for key, value in result.items():
        if key in TEXT_FIELDS and isinstance(value, str):
            lines, size = _counts(value)
            trimmed[key + "_lines"] = lines
            trimmed[key + "_bytes"] = size
        else:
            trimmed[key] = value
    if trimmed == result:
        return event               # nothing of the kind: the event goes as it is
    return {**event, "result": trimmed, "counted": True}
