#!/usr/bin/env python3
"""Record the compile timestamp embedded in a rebuilt binary, never its mtime."""
import datetime,hashlib,json,pathlib,re,sys
root=pathlib.Path(__file__).resolve().parents[1]
file,name,path,out=sys.argv[1:];file=pathlib.Path(file);raw=file.read_bytes();match=re.search(rb'([A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4} [0-9:]{8} UTC)\x00',raw)
if not match:raise SystemExit('No embedded UTC build date: '+str(file))
stamp=datetime.datetime.strptime(match[1].decode(),'%b %d %Y %H:%M:%S UTC').replace(tzinfo=datetime.timezone.utc).isoformat(timespec='seconds')
version=json.loads((root/'versions.json').read_text())[name]
item={'name':name,'path':path,'kind':'binary','version':version,'compiled_at':stamp,'sha256':hashlib.sha256(raw).hexdigest()}
if name=='zigma-curl-arm':item['upstream_version']='8.22.0'
pathlib.Path(out).write_text(json.dumps(item,indent=2)+'\n')
