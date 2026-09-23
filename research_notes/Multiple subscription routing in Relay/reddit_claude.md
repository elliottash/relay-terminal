# Multiple Claude Code subscription accounts (2025–2026)

## Do separate configuration directories support simultaneous subscription sessions?

### Takeaway
Yes for the Claude Code CLI: Anthropic explicitly describes `CLAUDE_CONFIG_DIR` as useful for accounts running side by side. Reddit has first-hand reports of two or three concurrent accounts working with separate launch environments, alongside narrower failure reports that do not establish a general prohibition.

### Cited Findings
- Anthropic says `CLAUDE_CONFIG_DIR` overrides `~/.claude`; settings, session history, and plugins live there, and its example launches `claude-work` with a separate directory. Its authentication page says `.credentials.json` follows that directory, including the macOS Keychain entry keyed to it. — [Environment variables](https://code.claude.com/docs/en/env-vars); [Authentication](https://code.claude.com/docs/en/authentication)
- In a June 18, 2025 thread, the WSL original poster reported that two directories showed different accounts in `/status` but appeared to share a usage limit. After a September 29 alias suggestion, replies on November 7 and December 10 said the setup worked for two projects and three accounts in parallel, respectively. These are self-reports; the original poster did not confirm a fix. — [Multiple Claude Max Accounts Running Claude Code](https://www.reddit.com/r/ClaudeAI/comments/1lepv7f/multiple_claude_max_accounts_running_claude_code/)
- An April 23–24, 2026 commenter described three Claude Code accounts launched in separate Kitty workspaces with `CLAUDE_CONFIG_DIR` and separate JSONL history. A July 30 tester reported that version 2.1.220 created a fresh `.claude.json` and empty MCP list in a new config directory while leaving the default config untouched. — [Two Max subscriptions on one Mac](https://www.reddit.com/r/Anthropic/comments/1stzm9p/how_you_guys_are_managing_two_claude_max/); [How to separate different Claude accounts](https://www.reddit.com/r/ClaudeCode/comments/1vamwrb/how_to_separate_different_claude_accounts/)
- A September 18, 2026 user said two config-scoped shell functions worked “for the most part” but reported apparent leakage, without isolating whether Claude Code, Claude Desktop, or integrations caused it. — [Simultaneous work and personal accounts on one Mac](https://www.reddit.com/r/ClaudeCode/comments/1wjbdjz/simultaneous_work_and_personal_accounts_on_one_mac/)

### Inferences
- For Relay, launch each Claude Code process with an absolute, stable account-specific `CLAUDE_CONFIG_DIR`, authenticate once in each, and verify `/status` for that process. This follows Anthropic's documented scope of the variable and credential precedence. — [Environment variables](https://code.claude.com/docs/en/env-vars); [Authentication](https://code.claude.com/docs/en/authentication)

### Gaps
- The Reddit reports do not independently verify server-side usage attribution for every account/version, and the June 2025 WSL failure did not identify its cause. — [June 2025 thread](https://www.reddit.com/r/ClaudeAI/comments/1lepv7f/multiple_claude_max_accounts_running_claude_code/)

## How does this differ from switching accounts or isolating other surfaces?

### Takeaway
`/login` changes the credential in a Claude Code configuration; per-process config directories preserve separate CLI credentials and histories concurrently. Containers, OS users, Desktop app profiles, and browser profiles isolate different state and should not be treated as equivalent CLI mechanisms.

### Cited Findings
- Anthropic documents `/logout` then re-authentication, and `claude auth login` for signing into an account. It stores CLI transcripts under `projects/<project>/<session>.jsonl`, with `claude -c` and `--resume` reopening local conversations; different config directories therefore have different local histories. — [Authentication](https://code.claude.com/docs/en/authentication); [Application data](https://code.claude.com/docs/en/claude-directory); [CLI reference](https://code.claude.com/docs/en/cli-reference)
- In July 5, 2026, a two-subscription user described repeatedly using `/login` across several Cursor windows. A reply recommended assigning each terminal process its own config directory; the user's claimed extra token cost after switching was an observation, not a measured account-switching rule. — [How u guys manage multiple Claude Max Subscriptions?](https://www.reddit.com/r/ClaudeCode/comments/1uo5nvq/how_u_guys_manage_multiple_claude_max/)
- The June 2025 WSL thread proposed Docker and separate OS users; no follow-up there established that Docker fixed the original poster's usage-limit problem. Anthropic separately says browser login in containers/WSL may need a copied URL and pasted code. — [June 2025 thread](https://www.reddit.com/r/ClaudeAI/comments/1lepv7f/multiple_claude_max_accounts_running_claude_code/); [Authentication](https://code.claude.com/docs/en/authentication)
- An April 23, 2026 Mac user reported running two Claude **Desktop** instances using Electron `--user-data-dir`, but also observed their Code-tab JSONL files sharing `~/.claude/projects`; that is a Desktop-specific, undocumented workaround and is weaker evidence of data separation than the CLI config setting. Another user suggested a web browser profile for the second web account, separately from CLI config. — [Two Max subscriptions on one Mac](https://www.reddit.com/r/Anthropic/comments/1stzm9p/how_you_guys_are_managing_two_claude_max/); [Account switching between work and personal](https://www.reddit.com/r/ClaudeAI/comments/1u2z9cd/account_switching_between_work_and_personal/)

### Inferences
- Keeping distinct CLI directories avoids replacing a single saved `/login` identity and keeps account-specific transcripts separate; project files, repository instructions, and external integrations may still be shared if processes use the same working tree. — [Environment variables](https://code.claude.com/docs/en/env-vars); [Explore the .claude directory](https://code.claude.com/docs/en/claude-directory)

### Gaps
- Anthropic's cited docs do not promise that Desktop app state or third-party integrations obey `CLAUDE_CONFIG_DIR`; the September 2026 leakage report did not identify which component crossed accounts. — [Environment variables](https://code.claude.com/docs/en/env-vars); [September 2026 thread](https://www.reddit.com/r/ClaudeCode/comments/1wjbdjz/simultaneous_work_and_personal_accounts_on_one_mac/)

## What billing, credential, and policy caveats are supported?

### Takeaway
Subscription OAuth and Console API-key routing are distinct. Anthropic documents credential precedence and per-account plan usage, while Reddit claims that multiple paid accounts are categorically banned or categorically safe remain unverified policy interpretations.

### Cited Findings
- Anthropic says Pro/Max usage is shared across Claude web, desktop, mobile, and Claude Code **within the signed-in account**; an `ANTHROPIC_API_KEY` can override subscription use and incur Console pay-as-you-go charges. Its CLI authentication order also puts `ANTHROPIC_AUTH_TOKEN`, API keys, helper keys, and `CLAUDE_CODE_OAUTH_TOKEN` ahead of a saved subscription `/login`; `/status` shows the active method. — [Pro/Max help](https://support.claude.com/en/articles/11145838-use-claude-code-with-your-pro-or-max-plan); [Authentication precedence](https://code.claude.com/docs/en/authentication); [API-key help](https://support.claude.com/en/articles/12304248-manage-api-key-environment-variables-in-claude-code)
- A February 18, 2026 Reddit post claimed a machine-ID-linked ban because two logins seemed to draw on one account's usage. The post presents this as inference from one machine, without an Anthropic statement or diagnostic evidence proving a policy change. It should not be reported as an established ban. — [Claude just banned having multiple Max accounts](https://www.reddit.com/r/ClaudeCode/comments/1r7x2su/claude_just_banned_having_multiple_max_accounts/)
- A January 18, 2026 Reddit thread asks whether multiple Max subscriptions violate terms; one commenter says three accounts had worked without problems. Such reports establish individual experience, not permission or enforcement policy. — [TOS for multiple Max 20x subscriptions](https://www.reddit.com/r/ClaudeCode/comments/1qg8xan/tos_for_multiple_max_20x_subscriptions/)

### Inferences
- Relay should keep provider/API-key routing distinct from account-specific subscription launches and avoid assuming `/login` determines billing when a higher-priority credential is inherited. — [Authentication precedence](https://code.claude.com/docs/en/authentication); [API-key help](https://support.claude.com/en/articles/12304248-manage-api-key-environment-variables-in-claude-code)

### Gaps
- Anthropic's documented side-by-side config example is a technical capability statement, not an explicit terms ruling on one person's purchase of several paid subscriptions, quota rotation, or cross-account transcript transfer. The cited Reddit discussions do not settle those questions. — [Environment variables](https://code.claude.com/docs/en/env-vars); [TOS discussion](https://www.reddit.com/r/ClaudeCode/comments/1qg8xan/tos_for_multiple_max_20x_subscriptions/)
