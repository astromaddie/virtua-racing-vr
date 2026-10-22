#!/usr/bin/env python3
"""Bind Frame artifacts to exact compilation inputs, excluding docs and captures."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'build' / 'frame-build-inputs.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = {ROOT / 'Makefile', ROOT / 'tools/build_frame.sh'}
    for directory in ('src', 'third_party/picodrive'):
        paths.update(p for p in (ROOT / directory).rglob('*') if p.suffix in {'.c', '.h', '.inc'})
    paths.add(ROOT / 'tools/openxr_colour_probe.c')
    paths.add(ROOT / 'build/openxr-sdk-1.1.63/install/lib/libopenxr_loader.a')
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(paths)}


def verify():
    record = json.loads(MANIFEST.read_text())
    if record['inputs'] != inputs():
        raise RuntimeError('Compilation inputs changed since tools/build_frame.sh; rebuild before packaging')
    for name, expected in record['artifacts'].items():
        if digest(ROOT / 'build' / name) != expected:
            raise RuntimeError(f'Artifact changed since build manifest: {name}')
    return record


def main():
    if len(sys.argv) == 2 and sys.argv[1] == '--verify':
        verify()
        print('Frame build input identity: PASS')
        return
    binaries = ('vrde', 'vrde-vr', 'openxr-colour-probe')
    record = {'inputs': inputs(), 'artifacts': {name: digest(ROOT / 'build' / name) for name in binaries},
              'compiler': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0]}
    MANIFEST.write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
