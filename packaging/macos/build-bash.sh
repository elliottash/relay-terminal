#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build pinned, patched GNU Bash against the macOS SDK, with no Homebrew dependencies.
set -euo pipefail
prefix=${1:?usage: build-bash.sh PREFIX}
case $(uname -s) in Darwin) ;; *) echo 'This builds a native macOS runtime' >&2; exit 1;; esac
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/relay-bash.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 - "$here/runtime-pins.json" "$work" <<'PY'
import hashlib,json,sys,urllib.request
from pathlib import Path
pins=json.loads(Path(sys.argv[1]).read_text())['bash']
root=Path(sys.argv[2])
for item in [pins['source'], *pins['patches']]:
    data=urllib.request.urlopen(item['url']).read()
    if hashlib.sha256(data).hexdigest()!=item['sha256']:
        raise RuntimeError('Bash download checksum mismatch: '+item['url'])
    (root/item['url'].rsplit('/',1)[1]).write_bytes(data)
PY
tar -xzf "$work/bash-5.3.tar.gz" -C "$work"
(
  cd "$work/bash-5.3"
  for patchfile in "$work"/bash53-*; do /usr/bin/patch -p0 < "$patchfile"; done
  # Pin system tools and SDK libraries: users need neither Homebrew nor its Cellar paths.
  export PATH=/usr/bin:/bin:/usr/sbin:/sbin
  export CC=/usr/bin/clang
  export CFLAGS='-O2 -mmacosx-version-min=13.0'
  export LDFLAGS='-mmacosx-version-min=13.0'
  ./configure --prefix="$prefix" --disable-nls --without-bash-malloc --with-curses
  make -j3
  make install
)
"$prefix/bin/bash" --noprofile --norc -c '[[ $BASH_VERSION == 5.3.20* ]] && declare -A check=([native]=yes) && [[ ${check[native]} == yes ]]'
# Deliver corresponding source and the exact build recipe alongside the GPL runtime.
mkdir -p "$prefix/share/relay-source"
cp "$work/bash-5.3.tar.gz" "$work"/bash53-* "$here/build-bash.sh" "$here/runtime-pins.json" "$prefix/share/relay-source/"
cp "$work/bash-5.3/COPYING" "$prefix/share/relay-source/COPYING"
