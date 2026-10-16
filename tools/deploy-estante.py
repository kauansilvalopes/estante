from pathlib import Path
from ftplib import FTP, error_perm
import hashlib,json,urllib.request,argparse,ipaddress
parser=argparse.ArgumentParser(description="Upload a new Estante installation with SHA-256 readback")
parser.add_argument("--host", required=True, type=lambda value: str(ipaddress.IPv4Address(value)))
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
artifact=root/'dist/PPSA99841'
assert (artifact/'eboot.bin').is_file()
param=json.loads((artifact/'sce_sys/param.json').read_text())
assert param['titleId']=='PPSA99841'
with urllib.request.urlopen(f'http://{args.host}:6969/appdb',timeout=10) as response:
    titles=json.load(response)['titles']
assert not any(t['titleId']=='PPSA99841' for t in titles), 'Title already registered; inspect before replacing'
base='/data/homebrew/PPSA99841'
ftp=FTP(); ftp.connect(args.host,2121,timeout=20); ftp.login('anonymous',''); print(ftp.sendcmd('SELF'))
ftp.cwd('/data/homebrew')
entries=[name for name, facts in ftp.mlsd()]
if not any(p.rstrip('/').split('/')[-1]=='PPSA99841' for p in entries): ftp.mkd(base)
created={base}
files=sorted(p for p in artifact.rglob('*') if p.is_file())
critical=[artifact/'eboot.bin',artifact/'sce_sys/param.json']
files=[p for p in files if p not in critical]+critical
for local in files:
    remote=base+'/'+local.relative_to(artifact).as_posix()
    parent=remote.rsplit('/',1)[0]
    parts=parent[len(base):].strip('/').split('/')
    current=base
    for part in parts:
        if not part: continue
        current+='/'+part
        if current not in created:
            try: ftp.cwd(current)
            except error_perm: ftp.mkd(current)
            created.add(current)
    temporary=remote+'.partial'
    data=local.read_bytes()
    ftp.cwd(parent)
    existing={name for name,facts in ftp.mlsd()}
    if local.name in existing:
        check=hashlib.sha256(); ftp.retrbinary('RETR '+remote,check.update)
        assert check.digest()==hashlib.sha256(data).digest(), 'Existing file differs: '+remote
        print('Already verified',remote,flush=True)
        continue
    with local.open('rb') as stream: ftp.storbinary('STOR '+temporary,stream,blocksize=65536)
    check=hashlib.sha256(); ftp.retrbinary('RETR '+temporary,check.update)
    assert check.digest()==hashlib.sha256(data).digest(), 'Readback mismatch: '+remote
    ftp.rename(temporary,remote)
    print('Verified',remote,len(data),flush=True)
ftp.quit()
print('UPLOAD VERIFIED. Console launch still needs testing.')
