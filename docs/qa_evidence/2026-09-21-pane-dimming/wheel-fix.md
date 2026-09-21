# Alt+wheel fix — #RG0Z

Qt 5.15.13 on X11 emits `angleDelta=(-120,0)` for Alt+wheel down and `(120,0)` for up. The old handler read only y, consumed the event, and adjusted nothing. The handler now passes both components to the dimming state, which prefers y and falls back to x.

Validation:
- `scripts/relay-build --target relay-panedimming-tests`: passed.
- `ctest --test-dir build -R '^panedimming$' --output-on-failure`: passed. Regression covers both axes, both directions, partial notches, multiple notches and diagonal input.
- Native Qt widget probe under Xvfb using the production State and xdotool: unmodified wheel leaves dimming 0; Alt+wheel down emits (-120,0) and changes dimming to 5; Alt+wheel up emits (120,0) and restores 0.
- Shared app build initially blocked by concurrent model-picker changes: src/Pane.h references relay::pickModel, absent from the working ModelPicker.h. No model-picker files were changed by this fix.

Desktop verification: in the rebuilt app, hover a pane and Alt+scroll down/up; expect 5% dimmer/brighter steps without moving keyboard focus.

The exact committed tree passed the full app build gate; landed as `5bac2a5de27eeb539ec3ebd44d275d4348e1bf1e`. Native full-app Xvfb check with isolated config/data/runtime: four Alt+wheel-down events showed `paneDimOverlay`; four Alt+wheel-up events hid it. Verified via RELAY_QA_RECTS. The shared build remains blocked by concurrent model-picker edits; the tested executable is `/tmp/claude-1000/land/dim-wheel/verify/build/relay`. Board format check reports existing findings elsewhere, none for RG0Z.
