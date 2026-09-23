# Profile: build

`scripts/relay-profile build --target relay (local Ninja build in /tmp/relay-243T-final-cold)` · spark-dcc9 · commit `31e9ee10+dirty` · 2026-09-23T21:19:31Z

513 steps · 1m 15s wall · 7m 34s of compile time

| Output | Seconds | Share |
|---|---:|---:|
| `CMakeFiles/relay.dir/src/RelayWindowCore.cpp.o` | 37.3 | 8.2% |
| `CMakeFiles/relay.dir/src/RelayWindow.cpp.o` | 37.2 | 8.2% |
| `CMakeFiles/relay.dir/src/main.cpp.o` | 35.6 | 7.8% |
| `CMakeFiles/relay.dir/src/RelayWindowSettings.cpp.o` | 32.9 | 7.2% |
| `CMakeFiles/relay.dir/src/RelayWindowModels.cpp.o` | 32.1 | 7.1% |
| `CMakeFiles/relay.dir/src/PaneRuntime.cpp.o` | 30.6 | 6.7% |
| `CMakeFiles/relay.dir/src/PaneEvents.cpp.o` | 15.1 | 3.3% |
| `CMakeFiles/relay.dir/src/PaneUi.cpp.o` | 14.3 | 3.1% |
| `CMakeFiles/relay.dir/src/PaneSession.cpp.o` | 12.4 | 2.7% |
| `CMakeFiles/relay-board.dir/src/BoardPane.cpp.o` | 11.8 | 2.6% |
| `CMakeFiles/relay.dir/src/Pane.cpp.o` | 11.0 | 2.4% |
| `CMakeFiles/relay-conversations.dir/src/Conversations.cpp.o` | 6.2 | 1.4% |
| `CMakeFiles/relay-remotepane.dir/src/RemotePane.cpp.o` | 5.2 | 1.1% |
| `engine/CMakeFiles/relay-terminal-engine.dir/view/TerminalView.cpp.o` | 4.7 | 1.0% |
| `CMakeFiles/relay-modelcatalog.dir/src/ModelCatalog.cpp.o` | 4.3 | 0.9% |
| `CMakeFiles/relay.dir/src/RemoteShare.cpp.o` | 4.2 | 0.9% |
| `CMakeFiles/relay-filepanes.dir/src/FilePanes.cpp.o` | 3.9 | 0.9% |
| `CMakeFiles/relay-settings.dir/src/SettingsPane.cpp.o` | 3.6 | 0.8% |
| `CMakeFiles/relay-modelpicker.dir/src/ModelPicker.cpp.o` | 3.5 | 0.8% |
| `CMakeFiles/relay-conversations.dir/src/SessionInfo.cpp.o` | 3.0 | 0.7% |
| `CMakeFiles/relay-board.dir/src/BoardModel.cpp.o` | 2.8 | 0.6% |
| `CMakeFiles/relay-actionpalette.dir/src/ActionPalette.cpp.o` | 2.5 | 0.6% |
| `CMakeFiles/relay.dir/src/TestSuitesPane.cpp.o` | 2.4 | 0.5% |
| `CMakeFiles/relay-sharing.dir/src/SharingPane.cpp.o` | 2.3 | 0.5% |
| `CMakeFiles/relay-subagents.dir/src/SubagentsPanel.cpp.o` | 2.3 | 0.5% |

Raw: ninja_log build.json build.trace.json

Flame graph: `scripts/relay-speedscope /tmp/relay-243T-profile-final/build.trace.json`
