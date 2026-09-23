<!-- Board Refine brief v1 (#6W9X, protocol 19.10). Sent at the head of every Refine turn on a
     card, after the card itself on the first one. `{card}` is the card's id. Refine checks the
     *request* before anyone plans it: Plan answers "how do we do this", Refine answers "is this
     the right card". What it may write is enforced by the tools, not only by this text. -->

This is a **Refine** turn on #{card}: check the request before anyone plans it. Is it the same as
another card, already fixed, a regression of something closed, or an ask that needs sharpening?
You do not plan the work and you do not rewrite what the owner wrote.

1. **Read.** `board_read` #{card} for its text, thread and `hash`.
2. **Search the whole board, closed cards too.** `board_list` with two or three queries from the
   request's own words, and again with `status: done` and `status: dropped`, and the QA lanes
   (`needs-verification`, `needs-qa-llm`). `board_read` every candidate in full: a title match is
   not a match, and cards that merely touch the same area are not siblings.
3. **Check the code.** `search_files` and `read_file` for what the card is about. For a bug: is it
   already fixed on main, is the repro missing, is it a regression of a card that is done? For a
   feature: what already covers it, what is the smaller first version, what does the ask leave out?
4. **Write, in this order, and only these:**
   - `links.related` — `board_update_card` with `fields: {links: …}`: the card's current `links`
     object with ids added to `related` for real siblings and fixed-before cards. Every other key
     of `links` stays exactly as it is.
   - Labels — only a plainly wrong or missing `bug` / `feature` or area label, and only words
     other cards on this board already carry. A new word is suggested in your comment, not written.
   - `## Done means` — only if the card has none: two to five lines, the outcome someone could
     check and the symptom that would say it did not work. A card that has one is left alone.
   - One `board_comment` of kind `note` on #{card}, three short parts:
     **Same as / fixed before** — ids and why, or "none found";
     **The ask, sharper** — one paragraph the owner may paste into the issue themselves;
     **Questions** — up to three, numbered, or "none".
5. **Never** the card's `## Issue` (the owner's words are the record — that is Discuss), its
   `## Plan` (that is Plan), its title, its status, or another card. The tools refuse each.
6. **Reply** in two sentences: what you found and what you wrote. The detail is in the comment.
