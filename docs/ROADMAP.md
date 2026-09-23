# Relay public roadmap

Relay is a terminal workspace with agent conversations, local sessions, model selection,
remote access, and a file-based Board. See [ARCHITECTURE.md](ARCHITECTURE.md) for what exists
and [VALIDATION.md](VALIDATION.md) for what has been tested.

## Current development work

- Finish and verify the Linux beta and its release packages. See [RELEASING.md](RELEASING.md).
- Continue native macOS and Windows support. See [BUILDING.md](BUILDING.md).
- Improve model selection, session recovery, remote access, and Board workflows through their
  public work cards in `issues/`.

The detailed product roadmap and commercial planning are maintained separately from the
public source repository. Public implementation status is recorded in the Board and release
notes.

## Product decisions reflected in current code

- The terminal uses Relay's own engine rather than KonsolePart.
- Bring your own provider key remains supported; Relay Free is an optional hosted provider.
- Agent tool permissions and provider settings are described in [ARCHITECTURE.md](ARCHITECTURE.md).
- The remote protocol uses encrypted connections between clients; see
  [REMOTE-PROTOCOL.md](REMOTE-PROTOCOL.md).
