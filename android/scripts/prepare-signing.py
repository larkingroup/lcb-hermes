import json, os, secrets, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
folder=root/'.signing'
folder.mkdir(exist_ok=True)
if (folder/'upload.jks').exists():
    print('Existing upload key preserved.')
    raise SystemExit(0)
subprocess.run(['icacls',str(folder),'/inheritance:r','/grant:r',os.environ['USERNAME']+':(OI)(CI)F','SYSTEM:(OI)(CI)F'],check=True,capture_output=True)
password=secrets.token_urlsafe(40)
tools=json.loads((root/'.toolchain/paths.json').read_text())
env=os.environ.copy();env['LCB_SIGNING_PASSWORD']=password
subprocess.run([str(Path(tools['java_home'])/'bin/keytool.exe'),'-genkeypair','-keystore',str(folder/'upload.jks'),'-alias','lcb-hermes','-keyalg','RSA','-keysize','4096','-validity','10000','-dname','CN=LCB, O=Larkin Group','-storepass:env','LCB_SIGNING_PASSWORD','-keypass:env','LCB_SIGNING_PASSWORD'],env=env,check=True,capture_output=True)
(folder/'release.properties').write_text('storePassword='+password+'\nkeyPassword='+password+'\n')
print('Private upload key created. No credentials in source.')
