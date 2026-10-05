from pathlib import Path
import argparse
import hashlib
import re
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
args = parser.parse_args()
binary = args.binary.resolve()
assert binary.is_file() and binary.read_bytes()[:2] == b'MZ', 'Windows EXE required'
version = re.search(r'project\(lcb_hermes VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
out = root / 'dist' / version
out.mkdir(parents=True, exist_ok=True)
archive = out / f'lcb-hermes-{version}-windows-x64.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
    for source, name in [(binary, 'lcb-hermes.exe'), (root / 'README.md', 'README.md'),
                         (root / 'NOTICE.txt', 'NOTICE.txt'),
                         (root / 'vendor/cjson/LICENSE', 'cJSON-LICENSE.txt')]:
        bundle.write(source, 'lcb-hermes/' + name)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(out / 'SHA256SUMS.txt').write_text(digest + '  ' + archive.name + '\n', encoding='utf-8')
with zipfile.ZipFile(archive) as bundle:
    assert bundle.read('lcb-hermes/lcb-hermes.exe') == binary.read_bytes()
print(archive)
print(digest)
