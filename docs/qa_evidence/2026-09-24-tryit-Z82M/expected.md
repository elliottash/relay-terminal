<!-- relay:sealed expected.md - the expected result of the #Z82M Try-it. The verifier
     knows what to look for from the card; this file records it before the person tries. -->

# Expected — Relay Free images on a keyless install

The staged Relay has **no provider key of its own** (fresh profile, keyring off) and its
Relay Free points at the local gateway, which serves a `relay-image` role.

1. Typing **"Make me an image of a lighthouse"** and pressing Enter produces an agent turn
   that ends with a **picture of a lighthouse shown inline in the pane** — a dusk sky, a
   striped tower, a warm beam, a moon — with no key dialog and no "image needs an
   openrouter key" refusal.
2. The file exists in the project: `lighthouse.png` next to the conversation, a real PNG.
3. The **quota chip** in the top bar counts it: it names Relay Free and now says how many
   images are left today — something like `Free · 98% left · 4 images left` (the stage
   allows 5 per day).
4. Asking for a **second** image decrements the count again (`· 3 images left`).
5. The turn's cost line / media result names the `relay-free` provider, not `openrouter`.

Not expected: the picture's artistry (it is a drawn placeholder), speed (a local script),
or the chat text around it (scripted to one sentence).

Failure looks like: any key dialog or "needs a openrouter key" wording; no image inline;
a chip that never mentions images; the image not counted (still "5 images left" after one).
