#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
version=${1:?usage: package.sh VERSION [BUILD_DIR] [STAGE_DIR]}
build=${2:-build-macos}
stage=${3:-stage-macos}
case $(uname -m) in arm64) arch=arm64;; x86_64) arch=x64;; *) echo 'Unsupported macOS CPU' >&2; exit 1;; esac
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
cmake --install "$build" --config Release --prefix "$stage"
mkdir -p "$stage/bundle" dist
stage=$(cd "$stage" && pwd)
app="$stage/bundle/Relay.app"
python3 - "$stage" "$app" <<'PY'
from pathlib import Path
import shutil,sys
stage,app=map(Path,sys.argv[1:])
bundles=[p for p in stage.rglob('*.app') if 'bundle' not in p.relative_to(stage).parts]
if len(bundles)!=1: raise RuntimeError(f'Expected one installed app, got {bundles}')
shutil.copytree(bundles[0],app,symlinks=True,dirs_exist_ok=True)
if (stage/'share/relay').exists():
    shutil.copytree(stage/'share/relay',app/'Contents/Resources/relay',symlinks=True,dirs_exist_ok=True)
PY
resources="$app/Contents/Resources"
mkdir -p "$resources/python" "$resources/licenses" "$resources/relay/data/icons/hicolor/256x256/apps"
cp data/icons/hicolor/256x256/apps/org.relayterminal.Relay.png "$resources/relay/data/icons/hicolor/256x256/apps/"
python3 - "$app/Contents/Info.plist" "$version" <<'PYINFO'
import plistlib,sys
from pathlib import Path
path=Path(sys.argv[1]); info=plistlib.loads(path.read_bytes())
info.update(CFBundleName='Relay',CFBundleDisplayName='Relay',CFBundleIdentifier='org.relayterminal.Relay',
            CFBundleShortVersionString=sys.argv[2].split('-')[0],CFBundleVersion=sys.argv[2].split('-')[0],
            LSMinimumSystemVersion='13.0',CFBundleIconFile='Relay.icns',
            CFBundleURLTypes=[{'CFBundleTypeRole':'Editor','CFBundleURLName':'Relay workspace links','CFBundleURLSchemes':['relay']}])
path.write_bytes(plistlib.dumps(info))
PYINFO
iconset="$stage/Relay.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" data/icons/hicolor/512x512/apps/org.relayterminal.Relay.png --out "$iconset/icon_${size}x${size}.png" >/dev/null
  doubled=$((size * 2))
  sips -z "$doubled" "$doubled" data/icons/hicolor/512x512/apps/org.relayterminal.Relay.png --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$resources/Relay.icns"
# Build the independent shell before deploying frameworks. Relocation must not depend on PATH.
/bin/bash packaging/macos/build-bash.sh "$resources/bash"
python3 - "$arch" "$resources/python" <<'PY'
import hashlib,io,json,sys,tarfile,urllib.request
from pathlib import Path
item=json.loads(Path('packaging/macos/runtime-pins.json').read_text())['python'][sys.argv[1]]
data=urllib.request.urlopen(item['url']).read()
if hashlib.sha256(data).hexdigest()!=item['sha256']: raise RuntimeError('Python checksum mismatch')
root=Path(sys.argv[2])
with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
    for member in archive.getmembers():
        member.name=member.name.removeprefix('python/')
        if member.name and member.name!='python': archive.extract(member,root,filter='data')
PY
python="$resources/python/bin/python3"
"$python" -m pip install --only-binary=:all: --no-compile 'cryptography==46.0.5'
# -S workers receive only bundled imports through this explicit path, no user site packages.
export PYTHONPATH="$resources/relay/backend:$resources/relay:$resources/python/lib/python3.13/site-packages"
export RELAY_BASH="$resources/bash/bin/bash"
export PYTHONDONTWRITEBYTECODE=1
"$python" -S -c 'import cryptography; from relay_core import agent, board, keystore; import remote.gui_host'
"$python" -S tests/test_worker_encoding.py --worker "$resources/relay/backend/worker.py"
macdeployqt "$app" -always-overwrite -no-codesign
qtroot=$(cd "$(dirname "$(command -v macdeployqt)")/.." && pwd)
if [[ -d $qtroot/licenses ]]; then cp -R "$qtroot/licenses" "$resources/licenses/Qt"; fi
cp LICENSE "$resources/licenses/Relay-AGPL-3.0.txt"
cp engine/third_party/libvterm/LICENSE "$resources/licenses/libvterm.txt"
cp packaging/macos/README.md "$resources/licenses/macOS-beta.md"
printf 'Relay source: https://github.com/elliottash/relay-terminal/releases/tag/v%s\nGNU Bash source, patches and build recipe: ../bash/share/relay-source\nPython and dependencies: license files in ../python\n' "$version" > "$resources/licenses/NOTICE.txt"
# .pyc files can be written before signing, never after bundle resource sealing.
find "$resources" -name '__pycache__' -type d -prune -exec rm -rf '{}' +
python3 packaging/macos/audit-bundle.py "$app" --arch "$arch" --sign
image_root="$stage/dmg-root"
mkdir -p "$image_root"
ditto "$app" "$image_root/Relay.app"
ln -s /Applications "$image_root/Applications"
cp packaging/macos/README.md "$image_root/Read me.md"
hdiutil create -volname Relay -srcfolder "$image_root" -ov -format UDZO "dist/relay_${version}_macos_${arch}.dmg"
