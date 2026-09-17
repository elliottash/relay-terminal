source /tmp/claude-1000/sp/env.sh
cd /tmp/claude-1000/sp/work
rm -f /tmp/claude-1000/sp/dump.txt
cmake --build /tmp/claude-1000/spike-build --target relay-vterm-spike 2>&1 | grep -E "error"
startspike
t 'clear; ls -la --color=always'; k Return
t 'printf "\e[1mbold\e[0m \e[3mitalic\e[0m \e[4munder\e[0m \e[7mreverse\e[0m \e[9mstrike\e[0m \e[38;5;208m256-orange\e[0m \e[48;2;120;40;160mtruecolor-bg\e[0m\n"'; k Return
t 'printf "CJK: 漢字かな한글| emoji: \U1F389\U1F44D\U1F3FD| flag: \U1F1EF\U1F1F5| é| box: ┌─┐\n"'; k Return
t 'printf "0123456789012345678901234567890123456789\n"'; k Return
t 'printf "\e]8;;https://example.com/docs\e\\\\OSC8 link\e]8;;\e\\\\ plain; grep hit src/main.cpp:42:7 and ~/nonexistent\n"'; k Return
sleep 0.8
shot 02-bash-colors-unicode-osc8
k ctrl+shift+d
sleep 0.3
