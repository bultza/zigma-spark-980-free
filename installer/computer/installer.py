#!/usr/bin/env python3
"""Build and serve a vendor-free, per-user modular installation bundle."""
import argparse
import base64
import hashlib
import gzip
import datetime
import io
import ipaddress
import json
import re
import shlex
import struct
import subprocess
import socket
import ssl
import tarfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROBOT = HERE.parent / 'robot'
PROFILES = ('ssh', 'tools', 'full')

def digest(data):
    return hashlib.sha256(data).hexdigest()

def public_key(path):
    lines = [x.strip() for x in path.read_text().splitlines() if x.strip()]
    if len(lines) != 1:
        raise ValueError('Provide exactly one OpenSSH public key')
    fields = lines[0].split()
    if len(fields) < 2 or fields[0] not in ('ecdsa-sha2-nistp256', 'ssh-rsa'):
        raise ValueError('The original Dropbear requires ECDSA P-256 or RSA; use ssh-keygen -t ecdsa -b 256')
    try:
        raw = base64.b64decode(fields[1], validate=True)
        n = struct.unpack('>I', raw[:4])[0]
        if raw[4:4+n].decode() != fields[0]:
            raise ValueError('Key type mismatch')
    except (ValueError, UnicodeError, struct.error) as exc:
        raise ValueError('Malformed public key') from exc
    subprocess.run(['ssh-keygen', '-l', '-f', str(path)], check=True, capture_output=True)
    return ' '.join(fields[:2])

def files_for(profile, telegram=False, source_paths=None):
    files = {}
    def add(dest, path, mode=0o700):
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Missing regular build/source file: {path}')
        files[dest] = (path.read_bytes(), mode)
        if source_paths is not None:
            source_paths.add(path)
    if profile in ('tools', 'full'):
        add('tools/zigmactl', ROBOT/'build/zigmactl')
        add('tools/zigmactl-LICENSE-cJSON.txt', ROBOT/'vendor/cjson/LICENSE',0o644)
    if profile == 'full':
        for name in ('free-zigma-agent-arm','zigma-observe-arm','zigma-core-arm','zigma-network-arm',
                     'zigma-area-arm','zigma-zones-arm','zigma-maps-arm','zigma-options-arm'):
            add('stack/'+name, ROBOT/'build'/name)
        for name in ('zigma-mode','zigma-mode-backend','zigma-free-service','zigma-cron','zigma-settings'):
            add('stack/'+name, ROBOT/'src'/name)
        for name in ('index.html','app.js','style.css','nor.js'):
            add('stack/src/'+name,HERE.parent/'web/src'/name,0o644)
        add('stack/licenses/cJSON.txt',ROBOT/'vendor/cjson/LICENSE',0o644)
        add('stack/licenses/MbedTLS.txt',ROBOT/'vendor/mbedtls/LICENSE',0o644)
        add('init/S98zigma-free',ROBOT/'init/S98zigma-free')
        add('stack/activate', ROBOT/'activate.sh')
        add('init/S02zigma-storage', ROBOT/'storage.sh')
        for name in ('S01time','S03zigma-hostname','S96ntpsync','S97crond'):
            add('support/'+name, ROBOT/'init'/name)
        add('support/zigma-time-sync', ROBOT/'src/zigma-time-sync')
        add('support/configure-support.sh', ROBOT/'configure-support.sh')
        if telegram:
            add('stack/zigma-curl-arm',ROBOT/'build/zigma-curl-arm')
            add('stack/telegram-ca.pem',ROBOT/'build/telegram-ca.pem',0o644)
            # Packaging cannot silently drop notices for the third-party transport.
            add('stack/licenses/transport.txt',ROBOT/'build/telegram-licenses.txt',0o644)
    for name,(data,_) in files.items():
        if name.endswith('-arm') or name == 'tools/zigmactl':
            if data[:6] != b'\x7fELF\x01\x01' or data[18:20] != b'\x28\x00':
                raise ValueError(f'Expected little-endian ARM ELF: {name}')
    if profile == 'full':
        versions=json.loads((ROBOT/'versions.json').read_text())
        records=[]
        for path,(data,_) in files.items():
            name=path.rsplit('/',1)[-1]
            if name not in versions or name == 'S02zigma-storage': continue
            kind='binary' if data.startswith(b'\x7fELF') else 'script'
            if versions[name].encode() not in data:
                raise ValueError(f'Build does not contain expected version: {name}')
            record={'name':name,'path':('/root/bin/'+name if path.startswith('tools/') or path == 'support/zigma-time-sync' else '/root/zigma-free/'+path[6:]),
                    'kind':kind,'version':versions[name],'sha256':digest(data)}
            if path.startswith('init/'): record['path']='/root/etc/init.d/'+name
            if kind=='binary':
                stamp=re.search(rb'([A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4} [0-9:]{8}) UTC\x00',data)
                if not stamp: raise ValueError(f'Missing build provenance: {name}')
                record['compiled_at']=datetime.datetime.strptime(stamp[1].decode(),'%b %d %Y %H:%M:%S').replace(tzinfo=datetime.timezone.utc).isoformat()
            else:
                stamp=re.search(rb'packaged ([^)]*)',data)
                if stamp: record['packaged_at']=stamp[1].decode()
            records.append(record)
        files['stack/release.json']=((json.dumps({'items':records},indent=2)+'\n').encode(),0o600)
    return files

def firmware_checks():
    """Inventory vendor firmware; unknown components limit installation to SSH."""
    catalog = json.loads((HERE/'known-firmware.json').read_text())
    lines = ['firmware_bad=0', 'firmware_variant=unreviewed', 'case "$layout" in']
    for layout, variant in catalog['variants'].items():
        if layout not in ('direct', 'overlay'):
            raise ValueError('Unknown catalog layout')
        lines += [layout + ')', 'firmware_variant=' + shlex.quote(variant['name']),
                  "while read -r expected path; do",
                  ' actual=$(sha "$path" 2>/dev/null) || actual=',
                  ' if test "$actual" = "$expected"; then',
                  '  echo "Firmware PASS: $path SHA256=$actual"',
                  ' else',
                  '  echo "Firmware UNKNOWN: $path actual=${actual:-UNREADABLE} expected=$expected" >&2',
                  '  firmware_bad=1',
                  ' fi',
                  "done <<'ZIGMA_REVIEWED_FIRMWARE'"]
        for path, digest in variant['files'].items():
            if not re.fullmatch(r'/[A-Za-z0-9_./-]+', path) or not re.fullmatch(r'[a-f0-9]{64}', digest):
                raise ValueError('Invalid firmware catalog entry')
            lines.append(digest + ' ' + path)
        lines += ['ZIGMA_REVIEWED_FIRMWARE', ';;']
    lines += ['*) firmware_bad=1; echo "Firmware layout unreviewed; SSH access will be evaluated separately";;', 'esac',
              'if test "$firmware_bad" = 0; then echo "Firmware recognized: $firmware_variant"; else echo "Unreviewed firmware: tools/full blocked, compatible persistent SSH remains allowed"; fi',
              'if test "$firmware_bad" != 0 && test "$profile" != ssh; then profile=ssh; echo "Requested $requested_profile; installing SSH ONLY for investigation"; fi']
    return '\n'.join(lines)


def build(profile, key, base_url, name='Vacuum', telegram=False):
    if profile not in PROFILES or not re.fullmatch(r'[A-Za-z0-9_-]{1,40}', name):
        raise ValueError('Invalid profile or robot name')
    if telegram and profile != 'full':
        raise ValueError('Telegram requires full profile')
    if not re.fullmatch(r'http://[0-9.]+:[0-9]+(?:/[a-f0-9]{32})?',base_url):
        raise ValueError('Invalid server URL')
    hostname=re.sub(r'-+', '-', name.lower().replace('_','-')).strip('-')
    if not hostname:
        raise ValueError('Robot name must contain a letter or digit')
    files=files_for(profile,telegram)
    manifest={n:{'sha256':digest(d),'bytes':len(d),'mode':oct(m)} for n,(d,m) in files.items()}
    # Exact per-file checks before any installation; no device data in the bundle.
    sums=''.join(f'{v["sha256"]}  {n}\n' for n,v in sorted(manifest.items())).encode()
    files['SHA256SUMS']=(sums,0o600)
    archive=io.BytesIO()
    with tarfile.open(fileobj=archive,mode='w') as tar:
        for n,(data,mode) in sorted(files.items()):
            info=tarfile.TarInfo(n);info.size=len(data);info.mode=mode;info.mtime=0
            tar.addfile(info,io.BytesIO(data))
    bundle=gzip.compress(archive.getvalue(),mtime=0)
    substitutions={'PROFILE':profile,'PUBLIC_KEY':key,'SERVER_BASE':base_url,
        'BUNDLE_SHA':digest(bundle),'BUNDLE_KIB':str((len(bundle)+1023)//1024),
        'PAYLOAD_KIB':str(sum((len(d)+1023)//1024 for d,_ in files.values())+128),
        'STACK_KIB':str(sum((len(d)+1023)//1024 for n,(d,_) in files.items() if n.startswith('stack/'))+128),
        'ROBOT_NAME':name,'HOSTNAME':hostname}
    script=(ROBOT/'install.sh.in').read_text().replace('@@FIRMWARE_CHECKS@@', firmware_checks())
    for k,v in substitutions.items():script=script.replace('@@'+k+'@@',shlex.quote(v))
    if '@@' in script:raise ValueError('Unresolved template marker')
    plan={'profile':profile,'telegram':telegram,'robot_name':name,'files':manifest,
          'bundle_bytes':len(bundle),'bundle_sha256':digest(bundle),
          'activation':'SSH immediately; full local mode requires activate start then accept',
          'vendor_binaries_distributed':False}
    return script.encode(),bundle,plan

def handler_for(assets):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            content=assets.get(self.path)
            if content is None:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Length',str(len(content)))
            self.send_header('Content-Type','application/octet-stream')
            self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(content)
        def log_message(self,format,*args):
            # Log asset names only; never reflect arbitrary request contents.
            path = getattr(self, 'path', '')
            asset = path.rsplit('/', 1)[-1] if path in assets else 'rejected'
            print('Installer request:', self.client_address[0],
                  getattr(self, 'command', None) or 'invalid', asset,
                  'TLS' if isinstance(self.connection, ssl.SSLSocket) else 'HTTP', flush=True)
    return Handler

def session_tls(directory, host):
    """Ephemeral local TLS compatibility for the original WeBack downloader."""
    cert, key = directory/'server-cert.pem', directory/'server-key.pem'
    # Reserve private files before openssl writes them, independent of caller umask.
    for path in (cert, key):
        with path.open('xb'):
            path.chmod(0o600)
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '2',
                    '-subj', '/CN=Zigma local installer', '-addext', f'subjectAltName=IP:{host}'],
                   check=True, capture_output=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    return context

class InstallerServer(ThreadingHTTPServer):
    """HTTP and legacy TLS on one port, with handshake timeouts per worker."""
    def __init__(self, address, handler, tls_context):
        self.tls_context = tls_context
        super().__init__(address, handler)

    def finish_request(self, request, client_address):
        request.settimeout(10)
        try:
            if request.recv(1, socket.MSG_PEEK) == b'\x16':
                with self.tls_context.wrap_socket(request, server_side=True) as secure:
                    self.RequestHandlerClass(secure, client_address, self)
            else:
                self.RequestHandlerClass(request, client_address, self)
        except (OSError, ssl.SSLError):
            print('Installer connection:', client_address[0], 'closed or handshake failed', flush=True)


def usable_host(value):
    ip = ipaddress.IPv4Address(value)
    if ip.is_unspecified or ip.is_multicast or ip.is_loopback or ip.is_link_local or ip.is_reserved:
        raise ValueError('Use a unicast LAN IPv4 address reachable by the robot')
    return str(ip)

def discover_hosts():
    """Read local Linux interface state; no outbound connection or route guess."""
    try:
        result = subprocess.run(['ip', '-j', '-4', 'address', 'show', 'up'],
                                capture_output=True, text=True, check=True, timeout=5)
        interfaces = json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        raise ValueError('Cannot detect local IPv4 addresses (requires Linux iproute2). '
                         'Specify --host with the computer address reachable by the robot.') from exc
    candidates = set()
    for interface in interfaces:
        flags = interface.get('flags', [])
        if 'UP' not in flags or 'LOOPBACK' in flags or interface.get('operstate') == 'DOWN':
            continue
        for address in interface.get('addr_info', []):
            if address.get('family') != 'inet' or address.get('scope') != 'global':
                continue
            try:
                host = usable_host(address['local'])
            except (KeyError, ValueError):
                continue
            candidates.add((interface['ifname'], host, address['prefixlen']))
    return sorted(candidates)

def select_host(explicit=None):
    if explicit is not None:
        host = usable_host(explicit)
        print(f'Server address: {host} (explicit --host selection).')
        return host
    candidates = discover_hosts()
    if not candidates:
        raise ValueError('No usable IPv4 address found on an active interface. '
                         'Connect to the robot’s destination network or specify --host.')
    if len(candidates) != 1:
        choices = '\n'.join(f'  {name}: {host}/{prefix}  (--host {host})'
                            for name, host, prefix in candidates)
        raise ValueError('Multiple active interface/address candidates found:\n' + choices +
                         '\nSpecify --host for the network the robot will join after pairing. '
                         'The server must remain reachable there; do not select the robot AP '
                         'or a VPN/container network unless the robot can reach that address.')
    name, host, prefix = candidates[0]
    print(f'Server address: {host} ({name}, {host}/{prefix}); '
          'automatically selected because it is the only usable active IPv4 address.')
    print('The robot must be able to reach this address after joining its destination Wi-Fi.')
    return host

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',choices=PROFILES,default='ssh')
    p.add_argument('--pubkey',required=True,type=Path)
    p.add_argument('--host',help='Server LAN IPv4; auto-detected when exactly one active address is available')
    p.add_argument('--port',type=int,default=8000)
    p.add_argument('--name',default='Vacuum')
    p.add_argument('--telegram',action='store_true')
    p.add_argument('--output',required=True,type=Path,help='New private output directory')
    p.add_argument('--serve',action='store_true',help='Serve only this generated bundle')
    a=p.parse_args()
    try:
        ip=select_host(a.host)
        if not 1<=a.port<=65535:raise ValueError('Invalid listener port')
        base=f'http://{ip}:{a.port}'
        script,bundle,plan=build(a.profile,public_key(a.pubkey),base,a.name,a.telegram)
        a.output.mkdir(mode=0o700,parents=False,exist_ok=False)
        for name,data in [('install.sh',script),('bundle.tar.gz',bundle),('plan.json',(json.dumps(plan,indent=2)+'\n').encode())]:
            path=a.output/name;path.write_bytes(data);path.chmod(0o600)
        subprocess.run(['sh','-n',str(a.output/'install.sh')],check=True)
        print(json.dumps({k:v for k,v in plan.items() if k!='files'},indent=2))
        print('script_url:',base+'/install.sh')
        print('script_md5:',hashlib.md5(script).hexdigest())
        if a.serve:
            context = session_tls(a.output, str(ip))
            with InstallerServer((str(ip),a.port),handler_for({'/install.sh':script,'/bundle.tar.gz':bundle}), context) as server:
                print('Serving two allowlisted files over HTTP and legacy TLS. Stop with Ctrl+C after verifying SSH.',flush=True)
                try:server.serve_forever()
                except KeyboardInterrupt:pass
    except (OSError,ValueError,subprocess.CalledProcessError) as exc:p.error(str(exc))
if __name__=='__main__':
    from console_output import configure_output
    configure_output()
    main()
