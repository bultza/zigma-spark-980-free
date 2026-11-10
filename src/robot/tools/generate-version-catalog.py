#!/usr/bin/env python3
import json,pathlib
root=pathlib.Path(__file__).resolve().parents[1];v=json.loads((root/'versions.json').read_text())
text='/* Generated from versions.json; do not edit by hand. */\nstatic const struct {const char *name,*version;} zigma_versions[]={\n'+''.join(' {'+json.dumps(k)+','+json.dumps(val)+'},\n' for k,val in v.items())+'};\n'
p=root/'src/zigma_versions_generated.h'
if not p.exists() or p.read_text()!=text:p.write_text(text)
