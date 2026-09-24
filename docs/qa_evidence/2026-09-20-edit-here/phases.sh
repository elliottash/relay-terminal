# Sourced by drive.sh once Relay is up: the steps and the checks of the #SEJ2 run.
#
# Rows are reached the way a keyboard user reaches them — Filter, then Down to
# the first match (the focus lands in the view, the selection is real). Two
# things the probes in the card thread taught the harness:
#   * the explorer opens a row on a single click, so a row click is an
#     activation, not just a selection — the drive never clicks rows;
#   * every geometry read (the Filter line) must come from a shot of the
#     current layout, because panes open and close between phases.
# Hints pace themselves: the registry keeps 20 s between hints, so the ✎ hint
# of phase 07 and the menu hint of phase 08 are separated by a deliberate wait.
k ctrl+shift+d; sleep 3
shot 01-explorer
check "01 the explorer lists note.txt and readme.md (01-explorer)" said 01-explorer "note.txt"

not_said() { ! said "$1" "$2"; }
on_disk()  { grep -q -- "$2" "$work/$1"; }
on_log()   { grep -q -- "$1" "$opened"; }
root_shot() {
    import -window root "$out/$1.png"
    tesseract "$out/$1.png" - --psm 11 2>/dev/null >"$out/$1.txt"
}
to_first_row() {
    shot _nav   # fresh geometry: panes have opened or closed since the last one
    f=$(word_xy _nav "Filter")
    click_at "${f% *}" "${f#* }"; sleep 0.6
    k Down; sleep 0.6   # first visible match: note.txt, focus in the view
}

# 02 — Enter previews the file read-only: the content, and no Save button.
to_first_row
k Return; sleep 3
shot 02-enter
check "02 Enter previews note.txt (02-enter)" said 02-enter "first"
check "02 the preview is read-only: no Save button (02-enter)" not_said 02-enter "Save"

# 03 — Ctrl+Enter on the same row: the preview is editable and takes typing.
to_first_row
k ctrl+Return; sleep 3
t "scratch line"; sleep 1
shot 03-editable
check "03 Ctrl+Enter opens it editable: typing lands (03-editable)" said 03-editable "scratch"
check "03 the header grew a Save button (03-editable)" said 03-editable "Save"

# 04 — Ctrl+S saves: the notice says Saved and the bytes reach the disk.
k ctrl+s; sleep 1.5
shot 04-saved
check "04 the notice says Saved (04-saved)" said 04-saved "Saved"
check "04 the bytes are on disk (note.txt)" on_disk note.txt "scratch line"

# 05 — a dirty preview asks before it closes; Discard keeps the saved bytes.
t "second scratch"; sleep 0.5
k ctrl+w; sleep 2
root_shot 05-dirty-close
check "05 closing a dirty preview asks: Save / Discard / Cancel (05-dirty-close)" said 05-dirty-close "Discard"
d=$(word_xy 05-dirty-close "Discard"); [[ -n $d ]] && click_at "${d% *}" "${d#* }"
sleep 2

# 06 — Enter on the file again is still read-only, and shows what 04 saved, not what 05 discarded.
to_first_row
k Return; sleep 3
shot 06-readonly
check "06 Enter again previews the saved bytes (06-readonly)" said 06-readonly "scratch"
check "06 the discarded edit is gone (06-readonly)" not_said 06-readonly "second"
check "06 read-only again: no Save button (06-readonly)" not_said 06-readonly "Save"

# 07 — the ✎ Edit button: it makes the preview editable and teaches Ctrl+Enter.
e=$(word_xy 06-readonly "Edit"); [[ -n $e ]] && click_at "${e% *}" "${e#* }"
sleep 2
shot 07-edit-button
check "07 the ✎ button opens editing (Save appears, 07-edit-button)" said 07-edit-button "Save"
check "07 it teaches the chord: Next time … Ctrl+Enter (07-edit-button)" said 07-edit-button "Next"

# 08 — the menu's Open external teaches Shift+Enter and really hands the file out.
# Wait out the hint registry's 20 s gap first, so this hint is allowed to show.
sleep 21
k ctrl+w; sleep 1.5   # the ✎ path left nothing dirty: the preview closes at once
to_first_row
k shift+F10; sleep 1.2
root_shot 08-menu
check "08 the right-click menu offers Open external (08-menu)" said 08-menu "external"
m=$(word_xy 08-menu "external"); [[ -n $m ]] && click_at "${m% *}" "${m#* }"
sleep 1.5
shot 08-open-external
check "08 the menu teaches Shift+Enter (08-open-external)" said 08-open-external "Shift"
check "08 the desktop was asked to open note.txt (xdg-open.log)" on_log note.txt

# 09 — Shift+Enter hands readme.md to the desktop the same way.
to_first_row
k Down; sleep 0.6   # second row: readme.md
k shift+Return; sleep 1.5
check "09 Shift+Enter hands readme.md to the desktop (xdg-open.log)" on_log readme.md
shot 09-shift-enter
