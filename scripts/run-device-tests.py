import json, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
tools=json.loads((root/'.toolchain/paths.json').read_text())
adb=str(Path(tools['sdk'])/'platform-tools/adb.exe')
serial='emulator-5580'
def call(*args, **kwargs):
    return subprocess.run([adb,'-s',serial,*args],check=True,**kwargs)
call('install','-r',str(root/'app/build/outputs/apk/debug/app-debug.apk'))
call('install','-r',str(root/'app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk'))
for setting in ['window_animation_scale','transition_animation_scale','animator_duration_scale']:
    call('shell','settings','put','global',setting,'0',capture_output=True)
access=(root.parent/'hermes-truenas/dashboard-access.txt').read_text().splitlines()
password=next(s[10:] for s in access if s.startswith('Password: '))
fixture=json.dumps({'url':'http://192.168.0.50:9119','username':'vince','password':password}).encode()
call('shell',"run-as com.larkingroup.lcbhermes sh -c 'mkdir -p files; cat > files/fixture.json'",input=fixture,capture_output=True)
print('Private access supplied to emulator test only.',flush=True)
result=call('shell','am','instrument','-w','-e','class','com.larkingroup.lcbhermes.ClientTest#nativeClientConnectsBrowsesAndResumes','com.larkingroup.lcbhermes.test/androidx.test.runner.AndroidJUnitRunner',capture_output=True,text=True)
print(result.stdout,flush=True)
if 'OK (1 test)' not in result.stdout:raise SystemExit(1)
