#!/usr/bin/env python3
"""Fetch the newest Windows portable release, including prereleases, with checksum verification."""
from pathlib import Path
import hashlib
import io
import json
import urllib.request
import zipfile

root = Path(__file__).resolve().parents[2]
output = root / 'desktop/build/windows-portable'
api = 'https://api.github.com/repos/larkingroup/lcb-hermes/releases?per_page=50'

def download(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'lcb-hermes-portable-fetch'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()

releases = json.loads(download(api))
for release in releases:
    if release['draft']:
        continue
    asset = next((a for a in release['assets'] if a['name'].endswith('-windows-x64.zip')), None)
    checksums = next((a for a in release['assets'] if a['name'] == 'SHA256SUMS.txt'), None)
    if asset and checksums:
        break
else:
    raise SystemExit('No Windows portable release with checksums found.')

sums = download(checksums['browser_download_url']).decode()
expected = next((line.split()[0] for line in sums.splitlines()
                 if len(line.split()) >= 2 and line.split()[-1].lstrip('*') == asset['name']), None)
if not expected:
    raise SystemExit('Archive checksum missing from release.')
archive = download(asset['browser_download_url'])
actual = hashlib.sha256(archive).hexdigest()
if actual != expected:
    raise SystemExit('Release checksum mismatch; nothing installed.')
with zipfile.ZipFile(io.BytesIO(archive)) as package:
    candidates = [item for item in package.infolist() if Path(item.filename).name == 'lcb-hermes.exe']
    if len(candidates) != 1:
        raise SystemExit('Expected exactly one portable lcb-hermes.exe.')
    binary = package.read(candidates[0])
output.mkdir(parents=True, exist_ok=True)
(output / 'lcb-hermes.exe').write_bytes(binary)
(output / asset['name']).write_bytes(archive)
(output / 'release.json').write_text(json.dumps({'tag': release['tag_name'], 'url': release['html_url'],
                                               'archive': asset['name'], 'sha256': actual}, indent=2)+'\n')
print(f"Verified {release['tag_name']}: {output / 'lcb-hermes.exe'} ({len(binary):,} bytes)")
