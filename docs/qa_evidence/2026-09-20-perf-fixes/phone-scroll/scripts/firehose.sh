# SPDX-License-Identifier: AGPL-3.0-or-later
# 50 MB through the terminal at full speed — #3H5T scenario (c), the one the desktop's frame
# coalescing already protects the phone from.
f=${BIG:-/tmp/relay-3h5t-50mb.txt}
[ -s "$f" ] || python3 -c "import sys;open(sys.argv[1],'wb').write((b'y'*99+b'\n')*500000)" "$f"
cat "$f"
