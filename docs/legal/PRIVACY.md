<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Privacy information for Relay and its hosted services

**Draft under revision, 2026-09-23.** The previous draft's categorical statements about
telemetry and conversation-content retention have been withdrawn. This document is not a
final privacy policy.

## Where requests go

Requests made with a user's own provider key go to that provider. Relay Free and Pro requests
pass through Relay's gateway and then to the model provider serving the selected role. These
requests can contain prompts, prior conversation turns, and tool results such as command output
or file contents the agent read. The selected provider's privacy terms also apply.

Saved conversations and the local search index are stored on the user's computer. Remote
sharing sends session data through the configured connection to paired devices or invited
participants.

The hosted service processes requests and keeps operational records for routing, quota,
reliability, and abuse prevention. Specific retention periods, any optional research program,
and choices about sharing conversation data require a reviewed policy before they are offered
as product terms. This draft should not be used to infer a promise about content retention.
