<!-- relay:entry 20260919T200441Z-cz author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: ["theme"] → ["feature", "theme"]

<!-- relay:entry 20260919T213239Z-27 author=agent kind=evidence model=glm-5.3 pane=6399f229 turn=42e73bd9d9b44c25a6276e05b532d7a1/a510bc4f049d4062afa2c8aefc2dc785 -->
Measured on the shared tree while landing #SEDZ (2026-09-19, unrelated run): `ctest -R buttonfit`
fails — `stylesheetFontsStayAtOrAboveTheFloor()` reports `dark-copper: "font-size: 8.5pt" is under
the 9pt floor` (`relay::theme::FloorPt`). Every other buttonfit case passes. Flagging for this
card's verifier since Dark Copper is in its scope; not touched by the sort work.

<!-- relay:entry 20260919T223610Z-7w author=agent kind=note model=glm-5.3 pane=cec9c04e turn=42e73bd9d9b44c25a6276e05b532d7a1/f09829265a7f4d5fa07165dcfb610009 -->
Follow-up to the evidence note below: `buttonfit` is green again as of cda5c6ca ("The tab's
project chip reads at the 9pt floor like everything else", 2026-09-19) — the chip moved to the
floor rather than the floor moving. `ctest -R 'buttonfit'` passes 205/205 on the current tree.
