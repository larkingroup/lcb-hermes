import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request
import zipfile

root = Path(__file__).resolve().parents[1]
toolchain = root / '.toolchain'
toolchain.mkdir(exist_ok=True)
def read(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'lcb-hermes-build/0.1'}),timeout=90).read()
def download(url, dest, checksum):
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == checksum:
        return
    print('Downloading',dest.name,flush=True)
    with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'lcb-hermes-build/0.1'}),timeout=120) as response, dest.open('wb') as out:
        while chunk := response.read(1024*1024):
            out.write(chunk)
    assert hashlib.sha256(dest.read_bytes()).hexdigest() == checksum, 'Checksum mismatch: '+dest.name
    print('Checksum verified:',dest.name,flush=True)
def extract(archive, destination):
    with zipfile.ZipFile(archive) as z:
        z.extractall(destination)

assets=json.loads(read('https://api.adoptium.net/v3/assets/latest/21/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse'))
package=assets[0]['binary']['package']
jdk_zip=toolchain / 'temurin-21.zip'
gradle_zip=toolchain / 'gradle-8.13-bin.zip'
cli_zip=toolchain / 'android-cli.zip'
gradle_sha=read('https://services.gradle.org/distributions/gradle-8.13-bin.zip.sha256').decode().strip()
downloads=[(package['link'],jdk_zip,package['checksum']),('https://services.gradle.org/distributions/gradle-8.13-bin.zip',gradle_zip,gradle_sha),('https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip',cli_zip,'90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a')]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
    futures=[executor.submit(download,*d) for d in downloads]
    for f in futures:
        f.result()
jdk_root=toolchain / 'jdk'
if not jdk_root.exists():
    extract(jdk_zip,jdk_root)
java_home=next(jdk_root.iterdir())
if not (toolchain / 'gradle-8.13').exists():
    extract(gradle_zip,toolchain)
sdk=toolchain / 'sdk'
latest=sdk / 'cmdline-tools/latest'
if not latest.exists():
    stage=toolchain / 'cli-staging'
    extract(cli_zip,stage)
    latest.parent.mkdir(parents=True,exist_ok=True)
    (stage / 'cmdline-tools').rename(latest)
env=dict(os.environ,JAVA_HOME=str(java_home),ANDROID_HOME=str(sdk),ANDROID_SDK_ROOT=str(sdk))
env['PATH']=str(java_home/'bin')+os.pathsep+env['PATH']
manager=latest/'bin/sdkmanager.bat'
with (toolchain/'sdk-licenses.log').open('w') as log:
    subprocess.run([str(manager),'--sdk_root='+str(sdk),'--licenses'],input='y\n'*100,text=True,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Installing Android API 36 and build tools',flush=True)
subprocess.run([str(manager),'--sdk_root='+str(sdk),'platform-tools','platforms;android-36','build-tools;36.0.0','emulator'],input='y\n'*100,text=True,env=env,check=True)
(root/'local.properties').write_text('sdk.dir='+str(sdk).replace('\\','/').replace(':',r'\:')+'\n')
(toolchain/'paths.json').write_text(json.dumps({'java_home':str(java_home),'sdk':str(sdk),'gradle':str(toolchain/'gradle-8.13/bin/gradle.bat')},indent=2))
print('Android build toolchain ready',flush=True)
