---
id: ZA3S
type: work
status: executing
labels: [feature, mcp, plugins]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: b65a84fc-848b-48e8-8d87-d77e2cf7423d
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [person], human: required, criteria: '`pytest tests/test_mcp.py tests/test_mcp_oauth.py` passes against a fake AS + MCP server: discovery, CIMD/DCR/static client, PKCE, callback state check, token refresh, revoked token, sign-out, and no token in logs/events. A person signs in to one real hosted server from Options and sees its tools in a pane turn.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Owner in a Relay pane, 2026-09-26, after the plugin-ecosystem review (#XHXX)
links: {plans: [], commits: [], evidence: [], related: [SSRQ, 9M96, XHXX, 3KB7], github: null}
---
# MCP OAuth: sign in to remote MCP servers (GitHub, Linear, Figma, Sentry, Notion, Slack)

## Issue
Relay's MCP client (#SSRQ) speaks streamable HTTP but refuses OAuth, so the hosted MCP servers that recur across the Warp, Claude and Codex catalogs (#XHXX) cannot connect without a hand-pasted token. Add the MCP authorization flow (discovery, dynamic client registration, PKCE browser sign-in, refresh) with tokens in the OS keyring, a Sign in action in Options › Security › MCP servers, and imports that bring remote servers across.

> claim and plan both mcp oauth and latex
> — elliott · [session:45c2ee31a8d34d5184a33977aa649f5f](relay://session/45c2ee31a8d34d5184a33977aa649f5f) · 2026-09-26

## Done means
- A remote MCP server that answers 401 with OAuth metadata can be signed in from Options › Security › MCP servers (**Sign in…**) or from `python3 -m relay_core.mcp_config login <server>`. The browser opens the provider's consent page; after approval the server's tools reach pane agents and guests (through `relay_board`), with no token in any Relay file, log, protocol event or error text.
- Tokens are kept in the OS keyring (Secret Service, macOS Keychain, Windows Credential Manager, the same backends as `keystore.py`). An expired access token is refreshed without asking. A revoked or unrefreshable one fails that one call with "Sign in to <server> again", and the settings row shows **Signed out**.
- The flow follows the MCP authorization spec: protected-resource metadata, authorization-server discovery, PKCE S256, the `resource` parameter, and client registration by Client ID Metadata Document, then dynamic registration, then a configured `client_id`. A server that supports none of these says what to configure.
- **Sign out** removes the tokens (and a dynamically registered client) from the keyring. Remote servers imported from Claude Code, Codex or Warp arrive as signed out and need one sign-in each. Tokens are never copied from those tools.
- Tests use a local fake authorization server plus MCP server. A live sign-in to one real hosted server (Linear or Sentry, whichever the owner uses) is recorded in the evidence.

## Plan
**Goal.** Relay's MCP client signs in to hosted MCP servers by OAuth, so the integrations common to the Warp, Claude and Codex catalogs (#XHXX) connect without a hand-pasted token. It reuses #SSRQ's client, trust and guest bridge unchanged.

**Findings (main 3ec4c6d0).**
- `backend/relay_core/mcp_client.py`:
  - The HTTP transport is `_post` (around `:300`). Its headers come from `spec.headers` via `_headers()` (`:298`).
  - A 401 or 403 is turned into "needs authorization Relay does not have" (`:331`), and the module docstring says "OAuth is not spoken" (`:11`).
- `backend/relay_core/mcp_config.py`:
  - `ServerSpec` (`:75`) and the validator (`:198-225`) handle `url` and `headers`. `sse` is refused (`:204`), and unknown keys are reported (`_KNOWN`, `:57`).
  - The CLI is `main()` (`:441`).
- `backend/relay_core/keystore.py`:
  - Keys go in the keyring via `secret-tool`, macOS (`maccredentials.py`) or Windows (`wincredentials.py`), always on stdin, never in argv.
  - `_check_id` accepts only provider ids, so MCP needs its own id form.
- `backend/relay_core/mcp_import.py` already imports `url` servers from Claude Code, Codex (`bearer_token_env_var`, `http_headers`) and Warp.
- `src/McpSettings.cpp` draws the Options rows (Trust, Enable, Remove, Add, Import) and shells out to the `mcp_config` CLI.
- Guests reach MCP tools only through `relay_board` (`guest_board_bridge.py`), so tokens stay in the worker and guests need no change.
- No OAuth code exists yet (`rg -n 'pkce|code_verifier|redirect_uri' backend/` is empty).

**Steps.**
1. **`backend/relay_core/mcp_oauth.py`, stdlib only** (the same no-dependency rule as the client).
   - Discovery: the `WWW-Authenticate` `resource_metadata`, else `/.well-known/oauth-protected-resource`, then AS metadata (RFC 8414, falling back to OIDC).
   - Client registration, in order: a Client ID Metadata Document (a fixed HTTPS URL Relay publishes, if the AS advertises support), then dynamic registration (RFC 7591), then `oauth.client_id` (and optional `client_secret_env`) from the server entry.
   - The authorization-code grant with PKCE S256, `state`, and `resource` (RFC 8707). Scopes come from the resource metadata unless the entry overrides them.
   - Redirect to a one-shot loopback listener on `127.0.0.1:<ephemeral>/callback` that answers one request, checks `state`, and closes within 5 minutes.
   - The token exchange, then refresh with rotation.
2. **Token store.** Add `keystore.store_secret/lookup_secret/remove_secret(namespace, name)` with the id `mcp:<sha256(canonical url)[:16]>`, so renaming a server keeps its sign-in and two projects naming different URLs `github` never share one. The keyring holds `{access, refresh, expires_at, client}` as JSON. With no keyring, refuse with a sentence rather than write a file.
3. **Client.**
   - `_post` asks `mcp_oauth.bearer(spec)` for a token (refreshing if it expires within 60 s) when the entry has no `Authorization` header of its own.
   - On a 401 it refreshes once and retries once. If that fails, it raises `McpError("Sign in to <server> again: Options › Security › MCP servers")`.
   - Tokens never enter `McpError` text, logs or `stderr` capture. Add a redaction test for this.
4. **Config and CLI.**
   - Add an optional `oauth: {client_id?, client_secret_env?, scopes?}` block to `_KNOWN` and `ServerSpec`.
   - Add CLI commands `login <server>` (prints the URL and opens it with `relay-open`/`xdg-open`; `--no-browser` for SSH sessions, where the user opens the URL locally and pastes the redirected URL back), `logout <server>`, and an `auth` column in `list` (`signed in` / `signed out` / `static header` / `none`).
5. **Options UI.** `src/McpSettings.cpp` gives each URL row whose server needs OAuth a **Sign in…** or **Sign out** button and a status detail, running the CLI in the background with a Cancel. There is no new window. Add a shortcut hint only if a palette action is added.
6. **Import.** Remote rows from `mcp_import.py` come across as signed out with a note "sign in after import". A Codex `bearer_token_env_var` stays a `${VAR}` header, as now.
7. **Docs.** `docs/MCP.md` gets a Sign-in section. The bundled `mcp-servers` skill learns `login`/`logout`. `mcp_client.py`'s docstring changes.

**Risks and owner decisions** (defaults chosen; each is a question on the card).
- **Servers without dynamic registration or CIMD** (GitHub's hosted server needs a pre-registered OAuth app). The default is a per-entry `oauth.client_id` the user supplies. Relay registering its own apps with GitHub, Slack and others is a product and distribution decision for the owner.
- **Hosting the CIMD document** needs a stable HTTPS URL under a Relay domain. Until one exists, the flow skips CIMD and uses DCR.
- **Legacy SSE-only servers** stay refused. Most hosted servers now offer streamable HTTP.
- **SSH and headless Relay** can't receive a loopback redirect from a browser on another machine. The `--no-browser` paste-back path covers it.
- Tokens never go to a guest process, since guests call through `relay_board`. Keep it that way. Do not pass tokens into `--mcp-config`.

**Verify.**
- New `tests/test_mcp_oauth.py`, run against an in-process fake AS and MCP server over HTTP, covering:
  - discovery from the 401 and from the well-known URL;
  - DCR, then static `client_id`;
  - PKCE verifier/challenge, and a `state` mismatch refused;
  - token exchange, refresh on expiry, and a revoked refresh token giving "Sign in again";
  - logout removing the keyring entry (keystore faked);
  - no token string in the captured logs, events or errors.
- `tests/test_mcp.py` still passes.
- `ctest -R mcp` if McpSettings has a test, else a manual check under Xvfb.
- One live sign-in to a real hosted server, recorded in `docs/qa_evidence/<date>-mcp-oauth/`.

**Order.** Steps 1–3 are one commit (backend and tests). Steps 4 and 6 are the next. Steps 5 and 7 come last. Publish each with `relay-land submit HEAD --card '#ZA3S'`.
