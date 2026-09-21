# Existing text follows theme changes — K3RT

The view regression prints notes, italic recaps and addition/removal lines once,
then changes dark → light → dark palettes on the same widget. It checks actual pixels
in the grid, replacement prose and expanded fold rows, then pushes those same rows
into scrollback and checks a further palette change. The six surface PNGs capture
the same text before and after the live theme change under Xvfb.

LibVtermCore now preserves palette indices when capturing history and when resizing
history back into the screen. That also preserves indexed colors in saved ANSI output.
Literal RGB supplied by terminal programs remains literal RGB. Previously saved RGB
from older Relay builds has no semantic identity to recover; it remains unchanged.

Reproduce:

```sh
scripts/relay-build --target relay-engine-tests relay
RELAY_ENGINE_TEST=CoreTest build/engine/relay-engine-tests
RELAY_ENGINE_TEST=FoldLayerTest build/engine/relay-engine-tests
XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=xcb RELAY_ENGINE_TEST=ViewTest xvfb-run -a build/engine/relay-engine-tests existingInlineTextFollowsThemeAcrossGridProseAndFolds anIndexedColourFollowsTheSchemeItIsPaintedUnder aUserRowWearsItsRoleFromTheSchemeInForce
```

The application also refreshes existing character formats in its temporary transcript
widget. It maps Relay-generated fold colors to palette references when handing them
to the view. Fold/prose reverse video keeps diff backgrounds adaptive.

Board validation has pre-existing findings elsewhere; none names K3RT.

Verification result: commit `82680f0910eac2c84f1e2f64512013bb57621d23` built from
its exact tree, with both `relay` and `relay-engine-tests` targets. The complete
`relay-engine-tests` CTest target passed in 19.44 seconds. Xvfb rendering checks
passed separately (5 passes including setup/cleanup). The shared build was blocked
by another session's CMake reference to the not-yet-created `tests/modelspane_test.cpp`;
the landing build excludes uncommitted changes and has no such dependency.
