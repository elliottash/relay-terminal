# Profile: build

`scripts/relay-profile build --target relay (local Ninja build in /tmp/relay-243T-baseline-normal)` · spark-dcc9 · commit `31e9ee10` · 2026-09-23T21:26:21Z

504 steps · 1m 25s wall · 4m 14s of compile time

| Output | Seconds | Share |
|---|---:|---:|
| `CMakeFiles/relay.dir/src/main.cpp.o` | 60.7 | 23.9% |
| `CMakeFiles/relay-board.dir/src/BoardPane.cpp.o` | 11.2 | 4.4% |
| `CMakeFiles/relay-conversations.dir/src/Conversations.cpp.o` | 5.7 | 2.3% |
| `CMakeFiles/relay-remotepane.dir/src/RemotePane.cpp.o` | 5.1 | 2.0% |
| `engine/CMakeFiles/relay-terminal-engine.dir/view/TerminalView.cpp.o` | 5.0 | 2.0% |
| `CMakeFiles/relay-modelcatalog.dir/src/ModelCatalog.cpp.o` | 4.5 | 1.8% |
| `CMakeFiles/relay.dir/src/RemoteShare.cpp.o` | 4.0 | 1.6% |
| `CMakeFiles/relay-settings.dir/src/SettingsPane.cpp.o` | 3.9 | 1.5% |
| `CMakeFiles/relay-filepanes.dir/src/FilePanes.cpp.o` | 3.6 | 1.4% |
| `CMakeFiles/relay-modelpicker.dir/src/ModelPicker.cpp.o` | 3.2 | 1.3% |
| `CMakeFiles/relay-conversations.dir/src/SessionInfo.cpp.o` | 3.1 | 1.2% |
| `CMakeFiles/relay-board.dir/src/BoardModel.cpp.o` | 3.0 | 1.2% |
| `CMakeFiles/relay-jobstab.dir/src/JobsTab.cpp.o` | 2.5 | 1.0% |
| `CMakeFiles/relay-actionpalette.dir/src/ActionPalette.cpp.o` | 2.5 | 1.0% |
| `CMakeFiles/relay-board.dir/src/BoardSignals.cpp.o` | 2.4 | 0.9% |
| `CMakeFiles/relay-appcommands.dir/src/AppCommands.cpp.o` | 2.4 | 0.9% |
| `CMakeFiles/relay-sharing.dir/src/SharingPane.cpp.o` | 2.3 | 0.9% |
| `CMakeFiles/relay-internals.dir/src/AgentInternalsView.cpp.o` | 2.2 | 0.9% |
| `CMakeFiles/relay-subagents.dir/src/SubagentsPanel.cpp.o` | 2.2 | 0.9% |
| `CMakeFiles/relay-subagents.dir/src/SubagentTranscript.cpp.o` | 2.2 | 0.9% |
| `CMakeFiles/relay-board.dir/src/BoardSections.cpp.o` | 2.1 | 0.8% |
| `CMakeFiles/relay-highlight.dir/src/Theme.cpp.o` | 2.1 | 0.8% |
| `CMakeFiles/relay.dir/src/TestSuitesPane.cpp.o` | 2.1 | 0.8% |
| `CMakeFiles/relay-localmodels.dir/src/LocalModelsSettings.cpp.o` | 2.1 | 0.8% |
| `CMakeFiles/relay.dir/src/Theme.cpp.o` | 2.1 | 0.8% |

Raw: ninja_log build.json build.trace.json

Flame graph: `scripts/relay-speedscope /tmp/relay-243T-profile-baseline-exact/build.trace.json`
