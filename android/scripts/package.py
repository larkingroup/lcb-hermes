from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile,re
root=Path(__file__).resolve().parents[1]
version=re.search(r"versionName '([^']+)'",(root/'app/build.gradle').read_text()).group(1)
out=root/'dist'/version;out.mkdir(parents=True,exist_ok=True)
for source,name in [('app/build/outputs/apk/release/app-release.apk',f'lcb-hermes-{version}.apk'),('app/build/outputs/bundle/release/app-release.aab',f'lcb-hermes-{version}.aab')]:
    shutil.copy2(root/source,out/name)
repo=root.parent
files=subprocess.check_output(['git','-C',str(repo),'ls-files'],text=True).splitlines()
with zipfile.ZipFile(out/'lcb-hermes-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in files:
        assert not any(part in name.split('/') for part in ['.signing','.toolchain','.test-deps','build']),name
        z.write(repo/name,'lcb-hermes/'+name)
lines=[]
for path in sorted(out.glob('*')):
    if path.is_file() and path.name!='SHA256SUMS.txt':lines.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name)
(out/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n')
print('Signed APK, AAB, source, and checksums packaged.')
