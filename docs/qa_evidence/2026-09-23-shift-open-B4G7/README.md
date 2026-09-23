# Shift+Enter attaches saved sessions

`drive.sh` uses an isolated Xvfb profile with a ranked guest default and two saved `local:stub` sessions. It searches for alpha and bravo in Sessions and presses Shift+Enter on each. The [final screenshot](02-after-bravo.png) shows the bravo row tagged `open` while Sessions retains its search and both new panes report `Session loaded`. The [first screenshot](01-after-alpha.png) shows the same for alpha.

The GUI log records `harness_deferred` for the original pane and `configure_sent ... preset=local:stub model=stub` for each new pane. `result.txt` confirms Sessions remained visible after two opens.
