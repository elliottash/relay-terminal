# Staging notes — #9NBZ Try it

The staged project is invented for this card: four small cards in a fresh board, one of which
("Parallax drift on the horizon strip") repeats the word *parallax* in three different sections,
so there is something to find and step through. The binary is this checkout's `build/relay`
(commit `bc1fda0f`, the find strip as landed). The run uses a throwaway HOME, XDG dirs and
TMPDIR under `/home/elliott/.cache/relay/scratch/tryit/9nbz/` — your real profile, keyring and
boards are not touched, and deleting that directory removes the whole fixture. The window opens
with `--clean-shell --fresh`, so it looks like a first run on a new machine (onboarding is
pre-answered in its config). Nothing else differs from real use: the board is a real board, the
cards are real cards, and the find strip is the landed code.

The list page's own find (the filter box, reachable with `/`) predates this card; Ctrl+F on the
list page now focuses it. The new surface is Ctrl+F with a card open.
