import hashlib,http.cookiejar,json,secrets,urllib.request,urllib.parse,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
version=re.search(r"versionName '([^']+)'",(root/'app/build.gradle').read_text()).group(1)
dist=root/'dist'/version
access=root.parents[1]/'hermes-truenas/dashboard-access.txt'
access_lines=access.read_text().splitlines()
base=next(s.split(': ',1)[1] for s in access_lines if s.startswith('Hermes dashboard: ')).rstrip('/')
username=next(s[10:] for s in access_lines if s.startswith('Username: '))
password=next(s[10:] for s in access_lines if s.startswith('Password: '))
jar=http.cookiejar.CookieJar()
opener=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
def request(path,data=None):
    req=urllib.request.Request(base+path,data=json.dumps(data).encode() if data is not None else None,headers={'Origin':base,'Content-Type':'application/json'})
    return json.load(opener.open(req,timeout=60))
request('/auth/password-login',{'provider':'basic','username':username,'password':password})
files=request('/api/files')
folder=files['path'].rstrip('/')+'/lcb-hermes/releases/'+version
download={}
for name in [f'lcb-hermes-{version}.apk',f'lcb-hermes-{version}.aab','SHA256SUMS.txt']:
    content=(dist/name).read_bytes()
    path=folder+'/'+name
    boundary='LCB'+secrets.token_hex(20)
    payload=(f'--{boundary}\r\nContent-Disposition: form-data; name="path"\r\n\r\n{path}\r\n--{boundary}\r\nContent-Disposition: form-data; name="overwrite"\r\n\r\nfalse\r\n--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{name}"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+content+f'\r\n--{boundary}--\r\n'.encode()
    req=urllib.request.Request(base+'/api/files/upload-stream',data=payload,headers={'Origin':base,'Content-Type':'multipart/form-data; boundary='+boundary})
    try:
        result=json.load(opener.open(req,timeout=90));path=result['path']
    except urllib.error.HTTPError as error:
        if error.code!=409:raise
    url=base+'/api/files/download?path='+urllib.parse.quote(path,safe='')
    with opener.open(url,timeout=90) as response:
        assert hashlib.sha256(response.read()).digest()==hashlib.sha256(content).digest(),'NAS copy mismatch'
    download[name]=url
(dist/'nas-downloads.json').write_text(json.dumps(download,indent=2)+'\n')
print('Copied and verified through the authenticated Hermes dashboard. No new ports.')
print('APK:',download[f'lcb-hermes-{version}.apk'])
