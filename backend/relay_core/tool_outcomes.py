# SPDX-License-Identifier: AGPL-3.0-or-later
"""Content-free tool result classification shared by native and guest records."""

OUTCOMES = frozenset(("success", "pending", "refused", "command_nonzero", "timed_out",
                      "transport_error", "internal_error", "unknown"))
ERROR_CODES = {
    "timeout": "timed_out", "deadline_exceeded": "timed_out",
    "connection_error": "transport_error", "transport_error": "transport_error",
    "internal_error": "internal_error", "refused": "refused",
    "permission_denied": "refused", "invalid_arguments": "refused",
}


def classify(name: str, result) -> tuple[str, str | None]:
    """Never interpret output/error prose or log a caller-provided arbitrary code."""
    if not isinstance(result, dict):
        return "unknown", "unclassified"
    if result.get("refused"):
        return "refused", "refused"
    code = result.get("error_code")
    if isinstance(code, str) and code in ERROR_CODES:
        return ERROR_CODES[code], code
    if result.get("timed_out"):
        if name == "agent_wait" and not result.get("error") and result.get("ok") is not False:
            return "pending", "poll_deadline"
        return "timed_out", "deadline_exceeded"
    exit_code = result.get("exit_code")
    if isinstance(exit_code, int) and not isinstance(exit_code, bool) and exit_code != 0:
        return "command_nonzero", "nonzero_exit"
    if result.get("error") or result.get("ok") is False or result.get("isError") is True:
        return "unknown", "unclassified"
    if result.get("still_running") is True:
        return "pending", "background_job"
    return "success", None


def exception_code(exc: Exception) -> str | None:
    """Only types with a trustworthy meaning; generic filesystem/encoding errors stay unknown."""
    if isinstance(exc, TimeoutError):
        return "deadline_exceeded"
    if isinstance(exc, ConnectionError):
        return "connection_error"
    if isinstance(exc, (AttributeError, NameError)):
        return "internal_error"
    return None
