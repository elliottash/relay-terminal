# Primary-source checks used for this implementation

> **Historical (status added 2026-09-17).** This page records sources checked for the first
> build on 2026-09-16 and is kept unchanged below. It is not maintained. Current design is in
> [ARCHITECTURE.md](ARCHITECTURE.md); all documents are listed in [README.md](README.md).
> Since then: Relay embeds `kf6/parts/konsolepart` or the KF5 part; the Kimi, `glm-coding` and
> OpenRouter presets were live-tested ([VALIDATION.md](VALIDATION.md)); the `glm` standard
> endpoint was not.

Sources were inspected during this build conversation (September 2026). This is a
reference snapshot, not an automatic updater or a claim that endpoints will never
change. No Warp implementation code was copied or bundled.

## Konsole / KDE

- Konsole Part header:
  https://raw.githubusercontent.com/KDE/konsole/master/src/Part.h
- Konsole Part implementation:
  https://raw.githubusercontent.com/KDE/konsole/master/src/Part.cpp
- KParts TerminalInterface:
  https://raw.githubusercontent.com/KDE/kparts/master/src/kde_terminal_interface.h
- Konsole Session implementation, including argument handling:
  https://raw.githubusercontent.com/KDE/konsole/master/src/session/Session.cpp
- KDE Kate's Konsole embedding implementation:
  https://raw.githubusercontent.com/KDE/kate/master/addons/konsole/kateconsole.cpp

Relevant findings: Konsole exposes an embeddable Part with `startProgram`,
`sendInput`, `terminalProcessId`, and `foregroundProcessId`. KDE's current Kate
integration loads `kf6/parts/konsolepart`. Konsole's Session argument handling
historically removes the first argument (the program name); Relay includes that
argument accordingly. These checks do not substitute for compilation or runtime
integration tests against the installed distro version.

## Qt editor

- https://doc.qt.io/qt-6/qplaintextedit.html

`QPlainTextEdit` is the native text-editor foundation. Relay adds submission
shortcuts, a conservative paste-size guard, simple shell coloring, draft-preserving
history, and an IME guard. Selection/clipboard/navigation use Qt behavior.

## Kimi

- https://platform.kimi.ai/docs/overview
  (The previously referenced platform.moonshot.ai quickstart redirected here.)

The inspected quickstart gives the OpenAI-compatible base URL
`https://api.moonshot.ai/v1`, model `kimi-k3`, and K3 reasoning-effort controls.
Relay's Kimi preset uses that configuration but leaves the model and parameters
editable. Actual user account/model access was not tested.

## GLM-5.3 / Z.AI

- https://docs.z.ai/guides/llm/glm-5.3

The inspected guide identifies `glm-5.3`, supports streaming/tool calling, and
states that disabling reasoning is not supported. It describes
`thinking.type: enabled` and reasoning effort `low`, `high`, or `max`.

The same page's protocol table lists the coding chat-completions base URL
`https://api.z.ai/api/coding/paas/v4`, while its cURL example uses
`https://api.z.ai/api/paas/v4/chat/completions`. Relay exposes both endpoint presets
and does not assume they have identical account eligibility or billing. This is
not a live verification of either endpoint with a personal API key.
