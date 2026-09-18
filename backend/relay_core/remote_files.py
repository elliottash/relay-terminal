# SPDX-License-Identifier: GPL-3.0-or-later
"""The agent's file tools on the host the user's terminal is logged into (card #S5SH).

`run_command` already takes `host` and runs over the user's own ssh connection
(relay_core/remote_session.py, docs/SSH-AND-MOSH.md section 7). The owner asked for the same for
the file tools: read_file, list_directory, write_file and edit_file take `host` too, so the agent
edits files on the host as comfortably as locally, with nothing installed there.

Everything here is a small POSIX shell script handed to the host through the same argv builder
`run_command` uses (`ssh -S <control socket> … -T <host> -- …`), wrapped in `sh -c` so it means the
same thing under a login shell that is bash, zsh, dash or ksh. The scripts use only `cat`, `head`,
`printf`, `dirname`, `chmod`, `cp`, `mv` and `rm`; a host missing one of them says so in the error.

Three rules the scripts keep, because the host has no workspace to confine the agent to:

- **Inside the user's home, or the directory their shell is in.** Every script opens with the same
  lines: the path with `~` expanded and made absolute against the remote `$PWD`, then a `case` that
  exits 78 unless it is `$HOME`, under `$HOME`, or under `remote_session.cwd`. `$HOME` is only known
  on the host, so the rule is checked there; the secret-name rule and `..` are refused here in
  Python (relay_core/tools.remote_path), before any ssh runs.
- **No symlinks**, as the local tools refuse them: reading or writing through one exits 77. The
  containment check is textual, so a *directory* symlink inside the home that points elsewhere is
  not caught — this is a guard against accidents, like the local workspace check, not a sandbox.
- **A half-written file is never visible.** Content travels on ssh's stdin (never as an argument:
  argv limits and quoting), into a temp file beside the target whose mode is taken from the target
  (`chmod --reference`, `cp -p` where that is not GNU), and `mv` puts it in place.
"""
from __future__ import annotations

import shlex
import subprocess

from . import remote_session

#: One file operation over the shared connection. A read or a write is a round trip on a connection
#: that is already open; a host that has not answered in this long is not going to.
TIMEOUT = 60
#: Entries a remote list returns, matching the local list_directory.
LIST_LIMIT = 200

# What a script's exit code means. Away from the shell's own (1, 2, 126–128) and ssh's 255, so a
# status from the host is never read as one of ours.
OUTSIDE = 78
UNREADABLE = 65
MISSING = 66
NO_PARENT = 68
WRITE_FAILED = 70
SYMLINK = 77
WRONG_TYPE = 79


def _guard(path: str, cwd: str | None) -> str:
    """The head of every script: `$p`, absolute on the host, inside the home or the shell's cwd."""
    roots = ['"$HOME"/*|"$HOME"']
    if cwd:
        quoted = shlex.quote(cwd)
        roots.append(f"{quoted}/*|{quoted}")
    return (f"p={shlex.quote(path)}\n"
            # `~/x` is a path models write; the remote shell never expands it here (the path is
            # quoted), so the script does, against the home the rule is about anyway.
            'case $p in "~") p=$HOME ;; "~/"*) p=$HOME/${p#"~/"} ;; esac\n'
            'case $p in /*) ;; *) p=${PWD:-$(pwd)}/$p ;; esac\n'
            f"case $p in {'|'.join(roots)}) ;; *) exit {OUTSIDE} ;; esac\n")


def read_script(path: str, cwd: str | None, *, cap: int, optional: bool = False) -> str:
    """`cat` the file, capped at `cap` bytes. `optional`: a missing file is exit 66 (and a missing
    parent directory 68) rather than an error — what a write's before-picture needs."""
    body = _guard(path, cwd)
    if optional:
        body += ('if [ ! -e "$p" ] && [ ! -L "$p" ]; then\n'
                 '  d=$(dirname -- "$p")\n'
                 f'  [ -d "$d" ] || exit {NO_PARENT}\n'
                 f'  exit {MISSING}\n'
                 'fi\n')
    else:
        body += f'[ -e "$p" ] || [ -L "$p" ] || exit {MISSING}\n'
    return (body
            + f'[ -L "$p" ] && exit {SYMLINK}\n'
            + f'[ -f "$p" ] || exit {WRONG_TYPE}\n'
            + f'[ -r "$p" ] || exit {UNREADABLE}\n'
            + f'cat -- "$p" | head -c {cap}\n')


def list_script(path: str, cwd: str | None, *, limit: int = LIST_LIMIT) -> str:
    """Name and kind of every entry, NUL-separated so a newline in a name cannot lie. One more than
    the limit is read, so the caller can say `truncated` exactly as the local tool does."""
    return (_guard(path, cwd)
            + f'[ -e "$p" ] || exit {MISSING}\n'
            + f'[ -d "$p" ] || exit {WRONG_TYPE}\n'
            + f'cd -- "$p" || exit {UNREADABLE}\n'
            + 'n=0\n'
              'for e in * .*; do\n'
              '  case $e in .|..) continue ;; esac\n'
              '  [ -e "$e" ] || [ -L "$e" ] || continue\n'
            + f'  n=$((n+1))\n'
              f'  [ $n -gt {limit + 1} ] && break\n'
            + '  if [ -L "$e" ]; then t=symlink; elif [ -d "$e" ]; then t=directory; else t=file; fi\n'
              "  printf '%s\\0%s\\0' \"$t\" \"$e\"\n"
              'done\n'
              'exit 0\n')


def write_script(path: str, cwd: str | None) -> str:
    """Read the new content from stdin into a temp file beside the target, with the target's mode,
    and `mv` it into place. The file is never seen half written, and never truncated on failure."""
    return (_guard(path, cwd)
            + 'd=$(dirname -- "$p")\n'
            + f'[ -d "$d" ] || exit {NO_PARENT}\n'
            + f'[ -L "$p" ] && exit {SYMLINK}\n'
            + f'if [ -e "$p" ] && [ ! -f "$p" ]; then exit {WRONG_TYPE}; fi\n'
            + 't=$p.relay-new.$$\n'
              'umask 077\n'
            + f': > "$t" || exit {WRITE_FAILED}\n'
            + 'if [ -e "$p" ]; then\n'
              '  chmod --reference="$p" -- "$t" 2>/dev/null || cp -p -- "$p" "$t" || '
            + f'{{ rm -f -- "$t"; exit {WRITE_FAILED}; }}\n'
              'fi\n'
            + f'cat > "$t" || {{ rm -f -- "$t"; exit {WRITE_FAILED}; }}\n'
            + f'mv -- "$t" "$p" || {{ rm -f -- "$t"; exit {WRITE_FAILED}; }}\n')


def argv(session: dict, script: str, cwd: str | None) -> list[str]:
    """The ssh command line: run_command's, with the script inside `sh -c`."""
    return remote_session.ssh_argv(session, "sh -c " + shlex.quote(script), cwd)


def run(session: dict, script: str, *, cwd: str | None = None, stdin: bytes = b"",
        env: dict | None = None, timeout: int = TIMEOUT) -> subprocess.CompletedProcess:
    """One file operation on the host. stdin carries the content of a write and nothing else."""
    try:
        return subprocess.run(argv(session, script, cwd), input=stdin, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, env=env, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise ValueError(f"{session.get('host')} did not answer within {timeout}s, so the file was left "
                         "alone. The connection may be wedged; ask the user whether their session is "
                         "still responding.") from None
    except OSError as error:
        raise ValueError(f"Relay could not start ssh for {session.get('host')}: {error}.") from None


def output(proc: subprocess.CompletedProcess, session: dict, path: str, *,
           wrong_type: str = "Only regular files are supported.") -> bytes:
    """The script's stdout, or a ValueError the model can act on."""
    code = proc.returncode
    if code == 0:
        return proc.stdout
    host = session.get("host")
    cwd = session.get("cwd")
    detail = (proc.stderr or b"").decode("utf-8", "replace").strip().splitlines()
    first = detail[0][:200] if detail else ""
    if code == OUTSIDE:
        where = f", and outside `{cwd}` (the directory their shell is in)" if cwd else ""
        raise ValueError(f"`{path}` is outside the home directory of the user's account on {host}{where}. "
                         "On a host the file tools stay inside the user's home directory and the directory "
                         "their shell is in — there is no workspace there to confine them. Ask the user to "
                         "look at a path outside it, or read it with run_command host if they agree.")
    if code == MISSING:
        raise ValueError(f"No such file or directory on {host}: `{path}`. List the directory first "
                         "(list_directory with host), or check the path with the user.")
    if code == SYMLINK:
        raise ValueError(f"`{path}` on {host} is a symlink, and Relay's file tools do not follow symlinks, "
                         "here or locally. Use the path it points at.")
    if code == WRONG_TYPE:
        raise ValueError(f"{wrong_type} `{path}` on {host} is not one.")
    if code == UNREADABLE:
        raise ValueError(f"`{path}` on {host} cannot be read with the user's own permissions "
                         f"({first or 'permission denied'}). Nothing is run as root over this connection.")
    if code == NO_PARENT:
        raise ValueError("Parent directory must already exist. Relay does not create directory trees "
                         f"automatically, on {host} any more than locally.")
    if code == WRITE_FAILED:
        raise ValueError(f"Writing `{path}` on {host} failed, and the file was left as it was"
                         + (f": {first}." if first else " (the directory may not be writable, or the disk full)."))
    if code == 255:
        raise ValueError(f"ssh exited 255: the connection to {host} failed or closed, so nothing was read or "
                         f"written{': ' + first if first else ''}. The user's session may have ended; ask them.")
    if code in (126, 127):
        raise ValueError(f"{host} is missing a command this needed ({first or 'command not found'}). "
                         "Relay's remote file tools need cat, head, printf, dirname, chmod, cp, mv and rm "
                         "on the host; use run_command host instead.")
    raise ValueError(f"The file operation on {host} failed (exit {code}){': ' + first if first else '.'}")


def entries(data: bytes, *, limit: int = LIST_LIMIT) -> tuple[list[dict], bool]:
    """A list script's NUL-separated output as list_directory's entries, sorted, plus `truncated`."""
    tokens = data.split(b"\x00")
    if tokens and tokens[-1] == b"":
        tokens.pop()
    found = []
    for i in range(0, len(tokens) - 1, 2):
        kind = tokens[i].decode("utf-8", "replace")
        found.append({"name": tokens[i + 1].decode("utf-8", "replace"),
                      "type": kind if kind in ("file", "directory", "symlink") else "file"})
    truncated = len(found) > limit
    return sorted(found[:limit], key=lambda item: item["name"]), truncated
