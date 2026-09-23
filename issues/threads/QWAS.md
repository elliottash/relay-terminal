<!-- relay:entry 20260923T012600Z-q1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:26 UTC
Owner, over three turns in this pane: "the most accessible keys are 1 2 3 q w e a s d z x c"; "i dont want a ctrl and ctrl shift to have different funcs"; "nobody has used relay yet so we can think fresh."; "qweasdzxc are really the sweet spot for accessibility"; "what if ctrl shift d is the file explorer. ctrl z is undo text edit; ctrl shift z is undo close. x and c reserved for cut and copy." The line on Z settles the modifier question that #KYPR left open: plain Ctrl stays the editor's where the key has an editing meaning.
<!-- relay:entry 20260923T012600Z-q2 author=claude-code kind=question -->
### Claude Code · 2026-09-23 01:26 UTC
1. The modifier rule as written in Planning notes: Ctrl+Shift+key is Relay's; plain Ctrl+key is the same Relay command only where the key has no editing or terminal meaning (W, E, N, T, Q in the prompt box), otherwise the editor's or the program's; never a different Relay command. Recommendation: yes, it is what your Z line implies, and it keeps Select All, Save, Undo, Cut, Copy, end-of-input and history working.
2. Ctrl+H / Ctrl+Shift+H are today the one letter pair with different commands (take control / back to the prompt). Make both one toggle? Recommendation: yes.
3. Globals become a tab inside Sessions & Projects and Ctrl+Shift+G is freed? Recommendation: yes, as #SPSG suggested.
<!-- relay:entry 20260923T012600Z-q3 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:26 UTC
Filed at the owner's "ready to put this on cards?". The map in Planning notes is a proposal read against `src/Keymap.h` at 275ac2ea; no bindings changed. Depends on #SPSG, #CPRQ and #MAGP landing first; #DKEW (Board on B) is superseded by A here.
