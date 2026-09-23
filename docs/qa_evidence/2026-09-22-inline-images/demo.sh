#!/usr/bin/env bash
# #1MGS: print one picture per protocol into the terminal, as a program would. Run in a Relay pane.
#   demo.sh <image dir>
dir=${1:-$(dirname "$0")}
b64() { base64 -w0 "$1"; }
echo "1. kitty graphics, PNG sent as data (a=T,f=100), in 4 KiB chunks:"
data=$(b64 "$dir/kitty.png"); first=1
while [[ -n $data ]]; do
    chunk=${data:0:4096}; data=${data:4096}; more=$([[ -n $data ]] && echo 1 || echo 0)
    if ((first)); then printf '\e_Ga=T,f=100,q=2,m=%s;%s\e\\' "$more" "$chunk"; first=0
    else printf '\e_Gm=%s;%s\e\\' "$more" "$chunk"; fi
done
echo
echo "2. iTerm2 inline image (OSC 1337 File=, width=30 cells):"
printf '\e]1337;File=inline=1;width=30:%s\a' "$(b64 "$dir/iterm.png")"
echo
echo "3. sixel:"
convert "$dir/sixel.png" sixel:-
echo
echo "4. text after the pictures is intact."
