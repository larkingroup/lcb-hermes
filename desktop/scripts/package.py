from pathlib import Path
import argparse
import hashlib
import re
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--label', default='', help='Optional development build label; keeps release packages separate')
args = parser.parse_args()
binary = args.binary.resolve()
assert binary.is_file() and binary.read_bytes()[:2] == b'MZ', 'Windows EXE required'
version = re.search(r'project\(lcb_hermes VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
assert not args.label or re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.label), 'Invalid build label'
build_name = version + ('-' + args.label if args.label else '')
out = root / 'dist' / build_name
out.mkdir(parents=True, exist_ok=True)
archive = out / f'lcb-hermes-{build_name}-windows-x64.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
    for source, name in [(binary, 'lcb-hermes.exe'),
                         (root / 'NOTICE.txt', 'NOTICE.txt'),
                         (root / 'vendor/cjson/LICENSE', 'cJSON-LICENSE.txt'),
                         (root / 'assets/NousResearch-LICENSE.txt', 'NousResearch-LICENSE.txt'),
                         (root.parent / 'branding/pc-workbench.png', 'pc-workbench.png'),
                         (root / 'docs/hermes-protocol.md', 'docs/hermes-protocol.md')]:
        bundle.write(source, 'lcb-hermes/' + name)
    readme = (root / 'README.md').read_text(encoding='utf-8').replace('../branding/pc-workbench.png', 'pc-workbench.png')
    if args.label:
        readme = f'Development build: {build_name}. Local preview; not a published release.\n\n' + readme
    bundle.writestr('lcb-hermes/README.md', readme)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(out / 'SHA256SUMS.txt').write_text(digest + '  ' + archive.name + '\n', encoding='utf-8')
with zipfile.ZipFile(archive) as bundle:
    assert bundle.read('lcb-hermes/lcb-hermes.exe') == binary.read_bytes()
    assert bundle.testzip() is None
    assert bundle.read('lcb-hermes/pc-workbench.png').startswith(b'\x89PNG\r\n\x1a\n')
    assert b'../branding/' not in bundle.read('lcb-hermes/README.md')
print(archive)
print(digest)
