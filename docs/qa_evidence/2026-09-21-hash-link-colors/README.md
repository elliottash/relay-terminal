# Hash-reference link coloring (#HCR2)

Targeted GUI tests passed under Xvfb with isolated XDG_CONFIG_HOME:

```
XDG_CONFIG_HOME=/tmp/relay-hash-config QT_QPA_PLATFORM=xcb RELAY_ENGINE_TEST=ViewTest xvfb-run -a build/engine/relay-engine-tests hashReferencesKeepLinkInkAfterRewrap markdownLinkLabelsAreClickable aPathInTheOutputWearsTheLinkColourAtRest
```

Result: 5 passed, 0 failed (including setup/cleanup), libvterm, Qt 5.15.13.
The hash regression checks pixels and click targets at widths 100, 40 and 120, with automatic coloring enabled and disabled. Known bold IDs are colored; unknown IDs stay plain. Screenshots use a deliberately unique blue test link color; the application uses the active theme link color.

Inspected libvterm-40.png: known IDs retain color and bold weight, unknown ID is plain, wrapped continuation stays aligned.

Implementation: fold/prose painting uses cached whole-logical-line link resolution, matching the hit-test scanner, with grid plain-ink, reverse, role and search protections.
