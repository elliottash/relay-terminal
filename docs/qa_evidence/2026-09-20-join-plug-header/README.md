# Join plug right of the gear, hairlines around it — #20XA (2026-09-20)

The header's right row is now the owner's decided order (2026-09-20): bell ‖ hairline ‖
Actions, Sessions, Switchboard, gear ‖ hairline ‖ join plug ‖ hairline ‖ minimize, maximize,
close. The plug's block moved from the far left of the row (before the bell) to after the tool
buttons, which end in the gear; the third hairline lives inside `!m_nativeFrame`, so under
`window/native_frame` the row ends … gear ‖ hairline ‖ plug with no trailing hairline.

## What was checked

`drive.py` (run under `Xvfb :58 -screen 0 1600x1000x24`, isolated profile, default dark-copper
theme unless noted) — all PASS, 2026-09-20:

    o-twelve-clusters — the row is 12 ink clusters: bell, hairline, Actions, Sessions,
        Switchboard, gear, hairline, plug, hairline, minimize, maximize, close
        (widths [12, 1, 10, 13, 14, 14, 1, 13, 1, 12, 12, 10])
    o-hairlines-at-2-7-9 — the 1px clusters sit at positions 2, 7 and 9; every other cluster is
        a glyph wider than 3px
    o-hairline-is-border-token — each hairline paints dark-copper's [ui] border #3a2822, the
        token read live from the theme file
    m-join-with-a-code / m-other-desktop — clicking the wide cluster between hairlines 2 and 3
        (the plug, right of the gear) opens the menu "Join with a code… / Open a pane your
        other desktop shares…" (OCR: implementer-02-plug-menu.png)
    b-bell-opens-list — clicking the row's first cluster opens the notifications list ("NOTIFICATIONS
        / Clear all / Nothing yet…", implementer-03-bell-popup.png)
    l-* — the same row under relay-light: 12 clusters, hairlines at 2, 7, 9 painted the light
        border #d3d7e0 (implementer-04-header-light.png)
    n-* — under window/native_frame: 8 clusters (bell, hairline, four tool buttons, hairline,
        plug), no trailing hairline, no window buttons, and the last button is the plug — its
        menu opens there too (implementer-05/06)

Also: `ctest -R panestatus` passes (toolButtons() unchanged); `scripts/relay-build` clean.

## Screenshots

- `implementer-01-header-dark.png`, `…-01b-…-zoom.png` — the row at 1× and 300%, default theme
- `implementer-02-plug-menu.png` — the plug's menu
- `implementer-03-bell-popup.png` — the bell's list
- `implementer-04-header-light.png`, `…-04b-…-zoom.png` — relay-light
- `implementer-05-header-native.png`, `…-05b-…-zoom.png`, `implementer-06-native-plug-menu.png`
  — window/native_frame

## Reproduce

    Xvfb :58 -screen 0 1600x1000x24 &
    cd docs/qa_evidence/2026-09-20-join-plug-header
    DISPLAY=:58 python3 drive.py [build-dir]
