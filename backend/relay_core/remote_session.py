# SPDX-License-Identifier: GPL-3.0-or-later
"""The ssh session the user's terminal is logged into, as the agent sees it (card #S5SH).

The GUI sends `remote_session` in an ask's context when the pane's foreground program is ssh or mosh
(docs/SSH-AND-MOSH.md section 7): the host alias, what it resolves to, the OpenSSH connection-sharing
socket Relay's wrapper set up, whether that socket answers, and the remote cwd. With it the agent is
told where the user is, and `run_command` takes `host` to run a command there over the user's own
authenticated connection (`ssh -S <socket>`): no second password, no second 2FA, no ssh agent.

Nothing here connects anywhere: it validates the context, writes the note the model reads, checks a
`host` argument against the session and builds the argv. relay_core.tools runs that argv through the
same job machinery as a local command.
"""
from __future__ import annotations

import os
import shlex
import stat

# Text fields and their caps. Unknown keys are dropped so the GUI can grow the object.
TEXT_FIELDS = {"program": 64, "host": 255, "hostname": 255, "user": 255, "control_path": 4096, "cwd": 4096}
BOOL_FIELDS = ("reachable", "shell_integration", "at_prompt")
CONNECT_TIMEOUT = 10


def _printable(text) -> str:
    return "".join(c for c in (text or "") if c.isprintable())


def validate(value) -> dict | None:
    """A clean copy of the context's `remote_session`, None when absent. Raises ValueError on a
    wrong type, an over-long string or a host that could be read as an ssh option."""
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValueError("Context remote_session must be an object.")
    out: dict = {}
    for key, limit in TEXT_FIELDS.items():
        item = value.get(key)
        if item is None:
            continue
        if not isinstance(item, str) or len(item) > limit or "\x00" in item:
            raise ValueError(f"Context remote_session.{key} must be text of at most {limit} characters.")
        out[key] = item
    host = out.get("host", "")
    if not host or host.startswith("-") or any(c.isspace() or not c.isprintable() for c in host):
        raise ValueError("Context remote_session.host must be a host name: printable, one word, "
                         "not starting with '-'.")
    for key in ("hostname", "user"):
        if key in out and any(not c.isprintable() for c in out[key]):
            raise ValueError(f"Context remote_session.{key} must be printable text.")
    if out.get("control_path") and not os.path.isabs(out["control_path"]):
        raise ValueError("Context remote_session.control_path must be an absolute path.")
    port = value.get("port")
    if port is not None:
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError("Context remote_session.port must be a number from 1 to 65535.")
        out["port"] = port
    for key in BOOL_FIELDS:
        item = value.get(key)
        if item is not None:
            if not isinstance(item, bool):
                raise ValueError(f"Context remote_session.{key} must be true or false.")
            out[key] = item
    return out


def usable(session: dict | None) -> bool:
    """Whether run_command may reach the host: the GUI saw the socket answer."""
    return bool(session and session.get("reachable") and session.get("control_path"))


def context_note(session: dict, *, delegated: bool = False) -> str:
    """The lines of the context note about the ssh session. Plain about which machine is which."""
    host = _printable(session.get("host"))
    user = _printable(session.get("user"))
    hostname = _printable(session.get("hostname"))
    program = _printable(session.get("program")) or "ssh"
    cwd = _printable(session.get("cwd"))
    port = session.get("port")
    where = f"{user}@{host}" if user else host
    extra = [hostname] if hostname and hostname != host else []
    if port and port != 22:
        extra.append(f"port {port}")
    lines = [f"The user's terminal pane is logged into {where}" + (f" ({', '.join(extra)})" if extra else "")
             + f" via {program}. The remote shell's current directory is "
             + (f"`{cwd}`." if cwd else "unknown.")]
    if session.get("at_prompt") is True:
        lines.append(f"The shell on {host} is at its prompt.")
    elif session.get("at_prompt") is False:
        lines.append(f"A program may be running in the session on {host}.")
    lines.append(f"Plain run_command runs on this local machine, not on {host}; read_file, list_directory, "
                 f"edit_file and write_file also only see this machine.")
    if usable(session):
        lines.append(
            f"To run a command on {host}, pass host: \"{host}\" to run_command. It runs over the user's own "
            f"ssh connection (no new login), non-interactively: no tty and no stdin, so nothing that asks "
            f"for a password (sudo) works there. It starts in "
            + (f"`{cwd}`" if cwd else f"the remote home directory")
            + f" unless you pass cwd, which is then a path on {host}. Read files on {host} with run_command "
            f"host (cat, sed -n, grep, ls), not read_file. Do not start your own ssh to {host}.")
    else:
        lines.append(
            f"Relay cannot share this {program} connection (no connection-sharing socket answers), so "
            f"run_command cannot reach {host}, and your own ssh would need a new login: do not try. Ask the "
            f"user to run what you need there and paste the output, or use run_in_terminal if it is offered.")
    if not delegated:
        lines.append(
            f"You cannot see the {program} session's screen or type into it: the user has not handed it to "
            f"you for this turn. If the request needs typing into that session itself, tell the user what "
            f"to type.")
    return "\n".join(lines) + "\n"


def check_host(session: dict | None, host: str) -> dict:
    """The session run_command may use for `host`, or a ValueError the model can act on."""
    if not session:
        raise ValueError(f"run_command host \"{host}\": the user's terminal is not logged into any host "
                         "over ssh right now, so there is no connection to reuse. Omit host to run on this "
                         "machine, or ask the user to ssh in first.")
    current = session.get("host", "")
    if host != current:
        raise ValueError(f"host \"{host}\" is not the host the user's terminal is logged into ({current}). "
                         f"run_command can only reach {current}, over the user's own connection; for another "
                         "machine ask the user, or use run_in_terminal if it is offered.")
    if not usable(session):
        raise ValueError(f"the ssh connection to {current} can't be shared (Relay found no connection-sharing "
                         "socket for it), so a command there would need a new login. Ask the user to run it in "
                         "their terminal, or use run_in_terminal if it is offered; reconnecting with Relay's ssh "
                         "wrapper on (Options › Terminal › SSH sessions) makes it shareable.")
    return session


def socket_alive(session: dict) -> bool:
    """The control socket still exists. Without it `ssh -S` would quietly open a new connection."""
    try:
        return stat.S_ISSOCK(os.stat(session["control_path"]).st_mode)
    except (OSError, KeyError, TypeError):
        return False


def remote_script(command: str, cwd: str | None) -> str:
    """What the remote login shell runs: the command, in `cwd` when one is known.

    `cd … || exit 1` on its own line rather than `cd … && command`: a multi-line command must not
    run its later lines in the wrong directory when the cd fails."""
    if cwd:
        return f"cd {shlex.quote(cwd)} || exit 1\n{command}"
    return command


def ssh_argv(session: dict, command: str, cwd: str | None) -> list[str]:
    """`ssh -S <socket> … -T <host> -- <script>`: the user's master connection, never a new login
    prompt (BatchMode), and no tty. ssh hands the script to the remote user's login shell."""
    return ["ssh", "-S", session["control_path"], "-o", "ControlMaster=no", "-o", "BatchMode=yes",
            "-o", f"ConnectTimeout={CONNECT_TIMEOUT}", "-T", session["host"], "--", remote_script(command, cwd)]
