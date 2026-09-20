# SPDX-License-Identifier: AGPL-3.0-or-later
# A streamed agent reply as a pane really shows one: wrapped prose with a coloured prefix,
# ten lines a second for twenty seconds (#3H5T scenario (a)).
i=0
while [ $i -lt 200 ]; do
  printf '\033[36m%3d\033[0m \033[1mrelay\033[0m the frame carries \033[33mrows\033[0m of style runs so the client needs no second emulator, line %d\n' $i $i
  i=$((i+1))
  sleep 0.1
done
