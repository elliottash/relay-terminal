# SPDX-License-Identifier: AGPL-3.0-or-later
"""Audit and ad-hoc sign every native binary in a relocatable macOS application."""
import argparse
import plistlib
from pathlib import Path
import subprocess

MAGIC = {bytes.fromhex(n) for n in ('feedface','feedfacf','cefaedfe','cffaedfe','cafebabe','bebafeca','cafebabf','bfbafeca')}

def native_files(bundle):
    for path in bundle.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in MAGIC:
                    yield path

def output(*args):
    return subprocess.check_output(args, text=True)

def audit(bundle, arch, sign):
    binaries = list(native_files(bundle))
    if not binaries:
        raise RuntimeError('Bundle has no Mach-O binaries')
    info = plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
    main_executable = bundle/'Contents/MacOS'/info['CFBundleExecutable']
    for path in binaries:
        arches = output('lipo','-archs',str(path)).strip().split()
        expected = 'x86_64' if arch == 'x64' else 'arm64'
        if expected not in arches:
            raise RuntimeError(f'{path}: missing native {expected}, found {arches}')
        # macdeployqt may retain a build-directory rpath that is unnecessary after deployment.
        lines = output('otool','-l',str(path)).splitlines()
        rpaths = []
        for i,line in enumerate(lines):
            if line.strip() == 'cmd LC_RPATH' and i+2 < len(lines):
                rpath = lines[i+2].strip().removeprefix('path ').split(' (offset')[0]
                if rpath.startswith('/') and not rpath.startswith(('/usr/lib','/System/Library')):
                    rpaths.append(rpath)
        if sign:
            for rpath in set(rpaths):
                subprocess.run(['install_name_tool','-delete_rpath',rpath,str(path)],check=True)
        elif rpaths:
            raise RuntimeError(f'{path}: external rpaths {rpaths}')
        for line in output('otool','-L',str(path)).splitlines()[1:]:
            if ' (compatibility version ' not in line:
                continue  # architecture header of a universal binary
            dep = line.strip().split(' (compatibility version ')[0]
            if dep.startswith(('/usr/lib/','/System/Library/','@rpath/','@loader_path/','@executable_path/')):
                continue
            raise RuntimeError(f'{path}: non-relocatable dependency {dep}')
        # Signing the main executable signs its enclosing app, which must wait until
        # every nested native component and framework has its final signature.
        if sign and path != main_executable:
            subprocess.run(['codesign','--force','--sign','-','--timestamp=none',str(path)],check=True)
    if sign:
        for framework in sorted(bundle.rglob('*.framework'),key=lambda p:len(p.parts),reverse=True):
            subprocess.run(['codesign','--force','--sign','-','--timestamp=none',str(framework)],check=True)
        subprocess.run(['codesign','--force','--sign','-','--timestamp=none',str(bundle)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict','--verbose=2',str(bundle)],check=True)
    print(f'PASS {len(binaries)} native Mach-O files: architecture, private/system linkage, signatures')

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('bundle',type=Path)
    parser.add_argument('--arch',choices=['arm64','x64'],required=True)
    parser.add_argument('--sign',action='store_true')
    args=parser.parse_args()
    audit(args.bundle.resolve(),args.arch,args.sign)
