#!/usr/bin/env python3
"""Package the dynamically linked experimental Linux client."""
from pathlib import Path
import argparse
import hashlib
import io
import tarfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, default=root/'desktop/build/linux/lcb-hermes')
args = parser.parse_args()
if not args.binary.is_file():
    raise SystemExit('Build the Linux client first.')
output = root/'desktop/dist'
output.mkdir(parents=True, exist_ok=True)
name = 'lcb-hermes-0.50-pre1-linux-experimental-x64'
archive = output/(name+'.tar.gz')
files = {'lcb-hermes': args.binary,
         'README.md': root/'linux/README.md',
         'NOTICE.txt': root/'desktop/NOTICE.txt',
         'cJSON-LICENSE.txt': root/'desktop/vendor/cjson/LICENSE'}
with tarfile.open(archive, 'w:gz') as package:
    for relative, path in files.items():
        info = package.gettarinfo(str(path), arcname=name+'/'+relative)
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        with path.open('rb') as source:
            package.addfile(info, source)
    run = b'#!/bin/sh\nset -eu\ncd -- "$(dirname -- "$0")"\nexec ./lcb-hermes "$@"\n'
    info = tarfile.TarInfo(name+'/run.sh')
    info.size, info.mode = len(run), 0o755
    package.addfile(info, io.BytesIO(run))
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(output/'SHA256SUMS-linux.txt').write_text(digest+'  '+archive.name+'\n')
print(f'{archive} ({archive.stat().st_size:,} bytes)')
