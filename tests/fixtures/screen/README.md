# Recorded terminal screens

The bottom rows of a terminal, one file per situation, used by `tests/screenprompt_test.cpp` to
check `relay::screen::detect` (`src/ScreenPrompt.cpp`).

Each file is the visible screen as `relay::TerminalBackend::screenText()` returns it: plain text,
one row per line, oldest row first. The classifier strips trailing whitespace, so a file may or
may not keep the space a program leaves after its prompt.

Add a file here rather than a string literal in the test: the point of the classifier is that it
is checked against the text programs really print.
