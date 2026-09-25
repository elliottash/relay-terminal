#!/usr/bin/env bash
# #7BYT Try-it staging. Builds the landed tree (1cc5b1d) in a scratch sandbox — not the shared
# checkout — seeds a fixture project whose output is full of clickable paths, and prints the one
# line that opens it. Rerunnable: the export and build are incremental, the fixture is rewritten.
set -euo pipefail
repo=/home/elliott/repos/relay-terminal
rev=1cc5b1d2772ace19d6a14cbd0d49ca24102ca9be
root=/home/elliott/.cache/relay/scratch/tryit/7byt
mkdir -p "$root/build" "$root/proj/src" "$root/proj/notes"
if [ ! -f "$root/src/.staged-$rev" ]; then
    rm -rf "$root/src"; mkdir -p "$root/src"
    git -C "$repo" archive "$rev" | tar -x -C "$root/src"
    touch "$root/src/.staged-$rev"
fi
cmake -S "$root/src" -B "$root/build" >/dev/null
cmake --build "$root/build" --target relay --parallel 8 >/dev/null 2>&1
cat > "$root/proj/src/boom.py" <<'EOF'
def main():
    return missing_thing + 1
main()
EOF
printf 'a note\n' > "$root/proj/notes/todo.md"
printf '# Fixture\n\npython3 src/boom.py prints a traceback of path:line links; ls src prints file links.\n' > "$root/proj/README.md"
echo "open:    $root/build/relay --workspace $root/proj --clean-shell --fresh"
echo "in it:   python3 src/boom.py && ls src"
