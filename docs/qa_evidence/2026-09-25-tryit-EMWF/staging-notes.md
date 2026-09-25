The fixture is a throwaway board (a git repo under the short run directory) seeded through the
real `BoardTools`, not your board: the requests are invented, and the session id in the quote
(`0f3ac2de…`) is a fixture id — no such conversation exists, so clicking the session link here
would find nothing. On a card filed by a live session the id is that pane's conversation and the
link opens it; that resolution is covered by the suite (OutputLinks `session:` targets and the
card page's `resolveAgentLink`, `Kind::Session`), so the Try it question is about the text, not
the click. The board is small (three cards) and can be deleted whole.
