# #VWSD — tab-meter give-way

Implementer evidence, not a QA verdict.

`drive.sh` starts `build/relay` under `Xvfb :204` with a fresh isolated HOME and XDG profile,
opens five long-named tabs through `/rename-tab`, then captures 2600, 1500, 1200, 900, 760, 640,
520, and 420 px windows. `implementer-420-after-clock.png` is six seconds after the narrow take;
the suffix must remain absent despite the tab meter's 5 s refresh. `implementer-2600-restored.png` verifies the
suffix returns on widening. The live bar's ordinary overflow behavior (scroll buttons, if Qt
chooses them) is preserved; this change only frees the suffix width when the full labels do not
fit.

## QA checklist

- [x] `scripts/relay-build`
- [x] `ctest --test-dir build -R 'paneusage|titles|boardworkspace' --output-on-failure`
- [ ] Run `drive.sh build` and inspect the width sweep: suffixes present while all full labels fit,
      then absent as a whole with names gaining the room.
- [ ] Confirm the 420 px before/after-clock pair does not flip the suffix back on, and the 2600 px
      restored capture has a current suffix again.
- [ ] Hover a narrow tab and confirm CPU/memory remains in its tooltip.
