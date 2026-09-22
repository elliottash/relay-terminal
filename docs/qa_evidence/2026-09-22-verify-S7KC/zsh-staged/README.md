# Zsh staging-only redraw comparison

Used latest shell/remote-integration.sh with __relay_r_redraw staging BUFFER rows, no zle reset-prompt. No build started. Drive exited 0.

Composer success and failure each initially show one prompt. **Ctrl+H after the no-provider inline message still duplicates the preceding zsh prompt**: compare 03-zsh-composer-failure.png (single prompt then red no-provider message) against 04-zsh-native-failure.png (red message gone, prior prompt twice, native command then its own single prompt). Thus changing zsh's Ctrl-X Ctrl-P widget is not sufficient to remove this narrower native-control/inline-output transition artifact. This does not affect clean captured command output.

Localhost SSH with deterministic worker stub; no paid model. Implementation untouched.
