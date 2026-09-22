# #ATP7 implementation evidence

## Root cause confirmed on Qt 6

The old `QIcon::pixmap(QSize(22,22) * qApp->devicePixelRatio())` request already receives DPR-aware output on Qt 6. Multiplying the requested logical size first doubles the scaling. Resetting the returned DPR to the same scale does not undo this. The original comment that QIcon ignores scale was true of this Qt5 configuration, but false on Qt6 (the sphinxpad build uses Qt6).

`dpi-probe.cpp` runs the exact old pixmap/QLabel path against the bundled 256px app asset and the replacement `QIcon::paint` path. `dpi-probe.log` records:

| Qt | DPR | Old QLabel logical size | Bounded paint target |
|---|---|---|---|
| 5.15.13 | 1 / 1.5 / 2 | 22 / 22 / 22 | 22 logical pixels |
| 6.4.2 | 1 / 1.5 / 2 | 22 / 33 / 44 | 22 logical pixels |

At DPR 2 Qt6 returns 88×88 physical pixels with DPR 2 for the old 44×44 request, creating a 44×44 logical icon. The replacement keeps the intended **22×22** logical box. The initial 18px implementation in `dcf941a7` is superseded: reducing the design size was unnecessary once the Qt6 bug was isolated.

Qt documents [QIcon high-DPI variants and DPR-aware pixmaps](https://doc.qt.io/qt-6/qicon.html): @2x images and scaled theme entries carry their resolution metadata; callers should not overwrite it. [QGuiApplication::devicePixelRatio](https://doc.qt.io/qt-6/qguiapplication.html#devicePixelRatio) is the highest DPR across screens, not necessarily the target window's. Painting into this widget uses its current paint device, avoiding the application maximum and stale startup raster. Physical mixed-monitor movement has not been exercised here.

## Reproduce

```sh
c++ -fPIC docs/qa_evidence/2026-09-22-ATP7/dpi-probe.cpp -o /tmp/atp7-probe-qt5 $(pkg-config --cflags --libs Qt5Widgets)
c++ -fPIC docs/qa_evidence/2026-09-22-ATP7/dpi-probe.cpp -o /tmp/atp7-probe-qt6 $(pkg-config --cflags --libs Qt6Widgets)
# Run each binary with QT_QPA_PLATFORM=offscreen and QT_SCALE_FACTOR=1, 1.5, 2.
scripts/relay-build --target relay
python3 docs/qa_evidence/2026-09-22-ATP7/stage.py
```

The live-app driver uses isolated XDG configuration under Xvfb. Geometry asserts 22×22 icon, 26×26 bell, and containment in the corner. Full application screenshots at 1×, 1.5×, and 2× retain physical pixels without resampling. These full-app stages use local Qt5; the direct old/new rendering probe separately exercises Qt6.

Board check has existing unrelated failures (12 errors, 754 warnings at the initial check), with none for ATP7. Board/delegation MCP methods and `tests_check` are not exposed in this guest; policy file fallback was used. No sphinxpad compositor check or physical mixed-monitor move was performed.

Initial implementation: `dcf941a726cdb35669aac435e4c0aa698c41d198`; its exact-tree build passed, and unrelated shared-header hunks were excluded. Follow-up restores 22px while retaining the DPI fix.

The @2x probe uses 22px and 44px files with Qt's naming convention; Qt6 at DPR2 returns 44 physical pixels with DPR2 for a 22px request. `qt6-compare-1.5.png` and `qt6-compare-2.png` visually compare the exact old pixmap path with painting in a 22px box; both inspected.

Final 22px implementation and DPR evidence: `2be99a0f125bca97534856dbf083fab24483d679`. Local Relay build `2026-09-22.19H.05`, live 1×/1.5×/2× geometry, and exact-tree landing build all passed.
