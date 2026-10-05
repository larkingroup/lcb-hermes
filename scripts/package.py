from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile
root=Path(__file__).resolve().parents[1]
out=root/'dist';out.mkdir(exist_ok=True)
for source,name in [('app/build/outputs/apk/release/app-release.apk','lcb-hermes-0.1.0.apk'),('app/build/outputs/bundle/release/app-release.aab','lcb-hermes-0.1.0.aab')]:
    shutil.copy2(root/source,out/name)
files=subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines()
with zipfile.ZipFile(out/'lcb-hermes-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in files:
        assert not any(part in name.split('/') for part in ['.signing','.toolchain','.test-deps','build']),name
        z.write(root/name,'lcb-hermes/'+name)
lines=[]
for path in sorted(out.glob('*')):
    if path.is_file() and path.name!='SHA256SUMS.txt':lines.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name)
(out/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n')
print('Signed APK, AAB, source, and checksums packaged.')
