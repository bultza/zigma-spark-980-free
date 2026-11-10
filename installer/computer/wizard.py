#!/usr/bin/env python3
"""Single-computer installation: run on the home Wi-Fi before entering robot AP."""
import argparse
import contextlib
import base64
import datetime
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.request
import webbrowser
from dataclasses import dataclass

import installer
import send_pairing


TRACE_STATE = threading.local()


def trace(message):
    if not getattr(TRACE_STATE, 'quiet', False):
        print(message, flush=True)


@contextlib.contextmanager
def quiet_traces(enabled):
    previous = getattr(TRACE_STATE, 'quiet', False)
    TRACE_STATE.quiet = previous or enabled
    try:
        yield
    finally:
        TRACE_STATE.quiet = previous


def choose_robot_wifi(network):
    print(f'Computer home Wi-Fi: {network.ssid!r}; installer address: {network.host}.')
    print('The robot needs a 2.4 GHz Wi-Fi network. A shared dual-band SSID is OK if 2.4 GHz is enabled.')
    print('The network name alone cannot prove which bands are available.')
    if any(hint in network.ssid.lower() for hint in ('5g', '5 g', '6g', '6 g')):
        print('WARNING: the current network name suggests 5/6 GHz. Do not send a 5/6-GHz-only SSID to the robot.')
    print('Enter the robot 2.4 GHz SSID, or press Enter to use ' + repr(network.ssid) + ':', flush=True)
    ssid = input().strip() or network.ssid
    if not ssid or any(c in ssid for c in '\"\\\r\n'):
        raise ValueError('SSID cannot be empty or contain quotes, backslashes or newlines.')
    print(f'Robot Wi-Fi selected: {ssid!r}. The computer will return to {network.ssid!r}.')
    print('Both networks must share the same LAN, with no guest/client isolation, so the robot can download from this computer.')
    if ssid != network.ssid or any(hint in ssid.lower() for hint in ('5g', '5 g', '6g', '6 g')):
        print('Confirm this SSID has 2.4 GHz enabled and can reach this computer. Type YES to continue:', flush=True)
        if input().strip().upper() != 'YES':
            raise ValueError('Wi-Fi selection cancelled before pairing. Connect the PC to your 2.4 GHz network and restart if unsure.')
    return ssid


def read_wifi_password(ssid):
    print(f'Password for ROBOT Wi-Fi {ssid!r}. Characters are VISIBLE while typing; press Enter when finished:', flush=True)
    return input()


def show_sent_wifi(ssid, password):
    # Explicit user request: terminal-only double-check after the single POST.
    print(f'Wi-Fi credentials submitted to robot: SSID={ssid!r}; PASSWORD={password!r}', flush=True)
    print('Check these values. A lost response does not prove rejection; the request will not be repeated. Do not share this screen.', flush=True)


def open_robot_web(url, token):
    print('CHANGE THIS TOKEN IMMEDIATELY in Settings > Web password after signing in.')
    print('Access token (copy this for Robot access): ' + token, flush=True)
    print('COPY THE TOKEN ABOVE, then press Enter to open the web page:', flush=True)
    input()
    trace(f'[BROWSER] Opening {url}; access token is not included in the URL.')
    opened = webbrowser.open(url)
    trace('[BROWSER] Open request sent.' if opened else '[BROWSER] No browser confirmed; open the displayed URL manually.')


def run(args, timeout=20):
    started = time.monotonic()
    trace(f'[SYSTEM] Starting {args[0]}; timeout={timeout}s; arguments/output withheld.')
    try:
        result = subprocess.run(args, check=True, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
    except (OSError, subprocess.SubprocessError) as exc:
        trace(f'[SYSTEM] {args[0]} failed after {time.monotonic() - started:.2f}s: {type(exc).__name__}; exit={getattr(exc, "returncode", "unknown")}.')
        raise
    trace(f'[SYSTEM] {args[0]} completed in {time.monotonic() - started:.2f}s; exit={result.returncode}; stdout={len(result.stdout)} characters.')
    return result.stdout.strip()


def powershell(script):
    return run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                '[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ' + script])


@dataclass
class Network:
    interface: str
    profile: str
    ssid: str
    host: str
    prefix: int

    def reconnect(self):
        trace(f'[WIFI] Requesting saved profile {self.profile!r} on {self.interface!r}; expected IPv4={self.host}.')
        if os.name == 'nt':
            run(['netsh', 'wlan', 'connect', 'name=' + self.profile,
                 'interface=' + self.interface], timeout=15)
        else:
            run(['nmcli', '--wait', '15', 'connection', 'up', 'uuid', self.profile,
                 'ifname', self.interface], timeout=20)


def windows_wifi():
    """Read WLAN connection fields through native API, independent of UI language."""
    import ctypes as C
    from ctypes import wintypes as W
    class GUID(C.Structure):
        _fields_ = [('a', W.DWORD), ('b', W.WORD), ('c', W.WORD), ('d', C.c_ubyte * 8)]
    class Interface(C.Structure):
        _fields_ = [('guid', GUID), ('description', W.WCHAR * 256), ('state', W.DWORD)]
    class SSID(C.Structure):
        _fields_ = [('length', W.DWORD), ('data', C.c_ubyte * 32)]
    class Association(C.Structure):
        _fields_ = [('ssid', SSID), ('bss', W.DWORD), ('mac', C.c_ubyte * 6),
                    ('phy', W.DWORD), ('index', W.DWORD), ('quality', W.DWORD),
                    ('rx', W.DWORD), ('tx', W.DWORD)]
    class Security(C.Structure):
        _fields_ = [('enabled', W.BOOL), ('oneX', W.BOOL), ('auth', W.DWORD), ('cipher', W.DWORD)]
    class Connection(C.Structure):
        _fields_ = [('state', W.DWORD), ('mode', W.DWORD), ('profile', W.WCHAR * 256),
                    ('association', Association), ('security', Security)]
    wlan = C.WinDLL('wlanapi')
    wlan.WlanFreeMemory.argtypes = [C.c_void_p]
    wlan.WlanFreeMemory.restype = None
    handle, version = W.HANDLE(), W.DWORD()
    def check(code):
        if code:
            raise OSError(code, 'Windows WLAN API failed. Enable Wi-Fi/location access for desktop apps and WLAN AutoConfig.')
    check(wlan.WlanOpenHandle(2, None, C.byref(version), C.byref(handle)))
    pointer = C.c_void_p()
    result = []
    try:
        check(wlan.WlanEnumInterfaces(handle, None, C.byref(pointer)))
        count = C.cast(pointer, C.POINTER(W.DWORD))[0]
        for i in range(count):
            item = Interface.from_address(pointer.value + 8 + i * C.sizeof(Interface))
            if item.state != 1:
                continue
            data, size, kind = C.c_void_p(), W.DWORD(), W.DWORD()
            check(wlan.WlanQueryInterface(handle, C.byref(item.guid), 7, None,
                                         C.byref(size), C.byref(data), C.byref(kind)))
            try:
                connection = C.cast(data, C.POINTER(Connection)).contents
                import uuid
                guid = str(uuid.UUID(bytes_le=bytes(item.guid)))
                # GUID originates in Windows, never user-supplied script text.
                addresses = json.loads(powershell(
                    "$a=Get-NetAdapter | Where-Object { [guid]$_.InterfaceGuid -eq [guid]'" + guid + "' }; "
                    "@(Get-NetIPAddress -InterfaceIndex $a.ifIndex -AddressFamily IPv4 | "
                    "Where-Object {$_.AddressState -eq 'Preferred'} | "
                    "Select-Object InterfaceAlias,IPAddress,PrefixLength) | ConvertTo-Json -Compress"))
                if isinstance(addresses, dict):
                    addresses = [addresses]
                ssid = connection.association.ssid
                for address in addresses:
                    result.append(Network(address['InterfaceAlias'], connection.profile,
                                          bytes(ssid.data[:ssid.length]).decode('utf-8'),
                                          address['IPAddress'], address['PrefixLength']))
            finally:
                wlan.WlanFreeMemory(data)
    finally:
        if pointer.value:
            wlan.WlanFreeMemory(pointer)
        wlan.WlanCloseHandle(handle, None)
    return result


def networks():
    if os.name == 'nt':
        candidates = windows_wifi()
    else:
        candidates = []
        lines = run(['nmcli', '-t', '--escape', 'no', '-f', 'UUID,TYPE,DEVICE',
                     'connection', 'show', '--active']).splitlines()
        for line in lines:
            profile, kind, interface = line.split(':', 2)
            if kind != '802-11-wireless':
                continue
            ssid = run(['nmcli', '-g', '802-11-wireless.ssid', 'connection', 'show', 'uuid', profile])
            for cidr in run(['nmcli', '-g', 'IP4.ADDRESS', 'device', 'show', interface]).splitlines():
                address = ipaddress.IPv4Interface(cidr)
                candidates.append(Network(interface, profile, ssid, str(address.ip), address.network.prefixlen))
    trace(f'[WIFI] Enumerated {len(candidates)} connected Wi-Fi IPv4 candidate(s).')
    for n in candidates:
        trace(f'[WIFI] Interface={n.interface!r}; profile={n.profile!r}; SSID={n.ssid!r}; IPv4={n.host}/{n.prefix}.')
    return [n for n in candidates if ipaddress.ip_address(n.host).is_private
            and not ipaddress.ip_address(n.host).is_link_local
            and not n.host.startswith('192.168.99.')]


def dependencies():
    trace('[PREFLIGHT] Checking Python libraries and system tools.')
    try:
        import paramiko
        import cryptography
    except ImportError as exc:
        raise ValueError('Missing Python dependencies. While connected to the Internet run:\n'
                         '  "' + sys.executable + '" -m pip install -r "' +
                         str(Path(__file__).with_name('requirements-wizard.txt')) + '"') from exc
    trace(f'[PREFLIGHT] Paramiko={paramiko.__version__}; cryptography={cryptography.__version__}.')
    required = ['netsh', 'powershell.exe', 'icacls'] if os.name == 'nt' else ['nmcli']
    for name in required:
        trace(f'[PREFLIGHT] {name}: {shutil.which(name) or "MISSING"}')
    missing = [name for name in required if not shutil.which(name)]
    if missing:
        raise ValueError('Missing tools: ' + ', '.join(missing) +
                         '. Linux requires NetworkManager (nmcli); Windows requires WLAN AutoConfig/PowerShell.')


def private_directory(path):
    trace(f'[FILES] Creating private session directory: {path}')
    path.mkdir(mode=0o700, parents=True, exist_ok=False)
    if os.name == 'nt':
        sid = powershell('[System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value')
        run(['icacls', str(path), '/inheritance:r', '/grant:r', '*' + sid + ':(OI)(CI)F'])
    else:
        path.chmod(0o700)


def tls_context(directory, host):
    trace(f'[TLS] Generating ephemeral RSA-2048 certificate for {host}; validity=2 days.')
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Zigma local installer')])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=5))
            .not_valid_after(now + datetime.timedelta(days=2))
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address(host))]), False)
            .sign(key, hashes.SHA256()))
    key_path, cert_path = directory/'server-key.pem', directory/'server-cert.pem'
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
    key_path.chmod(0o600)
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert_path, key_path)
    trace('[TLS] Certificate/key loaded; minimum TLS version=1.2; private key contents withheld.')
    return context


def observed_handler(assets, events, network):
    parent = installer.handler_for(assets)
    class Handler(parent):
        def do_GET(self):
            peer = self.client_address[0]
            asset = self.path if self.path in assets else '<not allowlisted>'
            trace(f'[DOWNLOAD] Request from {peer}; asset={asset}; transport={"TLS" if isinstance(self.connection, ssl.SSLSocket) else "HTTP"}.')
            if ipaddress.ip_address(peer) not in network:
                self.send_error(403)
                return
            super().do_GET()
            if self.path in assets:
                trace(f'[DOWNLOAD] Sent {len(assets[self.path])} bytes to {peer}: {self.path}. Transfer alone does not prove installation.')
                events.put((peer, self.path))
    return Handler


def wait_until(action, seconds, description, label=None, quiet=False):
    started = time.monotonic()
    deadline = started + seconds
    label = label or getattr(action, '__name__', 'condition')
    if not quiet:
        trace(f'[WAIT] {label}: starting; budget={seconds}s; retry interval=2s.')
    attempt = 0
    while time.monotonic() < deadline:
        attempt += 1
        if not quiet:
            trace(f'[WAIT] {label}: attempt={attempt}; elapsed={time.monotonic() - started:.1f}s; remaining={max(0, deadline - time.monotonic()):.1f}s.')
        with quiet_traces(quiet):
            result = action()
        if result:
            # Never log the return value: it may be an access token.
            trace(f'[WAIT] {label}: PASS after {attempt} attempt(s), {time.monotonic() - started:.1f}s.')
            return result
        if not quiet:
            trace(f'[WAIT] {label}: not ready; next check in 2s.')
        time.sleep(min(2, max(0, deadline - time.monotonic())))
    trace(f'[WAIT] {label}: TIMEOUT after {time.monotonic() - started:.1f}s. {description}')
    raise TimeoutError(description)


def ready_ap():
    try:
        with socket.create_connection(('192.168.99.1', 80), timeout=1):
            return True
    except OSError as exc:
        trace(f'[AP] TCP 192.168.99.1:80 not ready: {type(exc).__name__}; errno={exc.errno}.')
        return False


def restored(network):
    try:
        return any(n.interface == network.interface and n.profile == network.profile
                   and n.host == network.host for n in networks())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        trace(f'[WIFI] Cannot confirm original profile/IP yet: {type(exc).__name__}.')
        return False


def post_pairing(config):
    trace(f'[PAIRING] POST http://192.168.99.1/; App_Type=Redmond; script_url={config["script_url"]}; MD5={config["script_md5"]}; timeout=6s. Password/body withheld.')
    request = urllib.request.Request('http://192.168.99.1/',
        data=json.dumps(config, ensure_ascii=False).encode(), headers={'Content-Type': 'application/json'})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=6) as response:
        send_pairing.check_response(response.read(4097))
        trace(f'[PAIRING] HTTP {response.status}; acceptance JSON validated; response body withheld.')


class Robot:
    def __init__(self, host, key, directory):
        self.host, self.key, self.directory = host, key, directory
        self.host_key = None

    def command(self, command, timeout=15, diagnostic=None):
        import paramiko
        robot = self
        started = time.monotonic()
        trace(f'[SSH] Connecting root@{self.host}:22 with session key; connect/banner/auth timeouts=4s each.')
        class Pin(paramiko.MissingHostKeyPolicy):
            def missing_host_key(self, client, hostname, key):
                if robot.host_key is not None and robot.host_key != key:
                    raise ValueError('Robot SSH host key changed; stopping.')
                trace('[SSH] Host key ' + ('matches session pin.' if robot.host_key is not None else 'first seen; trusting this local network and pinning for this session.'))
                robot.host_key = key
        with paramiko.SSHClient() as client:
            client.set_missing_host_key_policy(Pin())
            client.connect(self.host, username='root', pkey=self.key, allow_agent=False,
                           look_for_keys=False, timeout=4, banner_timeout=4, auth_timeout=4)
            # Save only after authentication with this session's fresh private key.
            known = paramiko.HostKeys()
            known.add(self.host, self.host_key.get_name(), self.host_key)
            known.save(str(self.directory/'known_hosts'))
            trace(f'[SSH] Public-key authentication succeeded; known_hosts saved. Executing: {command}; timeout={timeout}s.')
            stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
            stdin.close()
            from ssh_output import collect, report, redact
            out, error, code, timed_out, counts = collect(stdout.channel, timeout)
            trace(f'[SSH] Command completed in {time.monotonic() - started:.2f}s; exit={code}; stdout={counts[0]} bytes; stderr={counts[1]} bytes; timeout={timed_out}.')
            if diagnostic or code != 0 or timed_out:
                saved = None
                try:
                    saved, details = report(self.directory, command, out, error, code, timed_out, counts, diagnostic)
                    print('[DIAGNOSTIC] Saved: ' + str(saved))
                except OSError as exc:
                    print('[DIAGNOSTIC] Cannot save report: ' + type(exc).__name__)
                if diagnostic:
                    for line in redact(out.decode('utf-8', 'replace')).splitlines():
                        print('[REMOTE stdout] ' + line)
                for line in redact(error.decode('utf-8', 'replace')).splitlines():
                    print('[REMOTE stderr] ' + line)
                if any(n > 1024*1024 for n in counts):
                    print('[DIAGNOSTIC] Output exceeded the 1 MiB per-stream limit; retained prefix only.')
                if code != 0 or timed_out:
                    reason = 'timed out' if timed_out else f'failed (exit={code})'
                    message = redact(error.decode('utf-8', 'replace')).strip()[-1500:] or 'Remote command returned no stderr.'
                    raise RuntimeError(f'Robot command {reason}: {message}' + (f' Report: {saved}' if saved else ''))
            return out.decode('utf-8').strip()

    def completed_profile(self, bundle_sha):
        import paramiko
        try:
            result = self.command('test ! -e /root/.zigma-install/pending && cat /root/.zigma-install/transaction-*/session-result')
            lines = result.splitlines()
            for index in range(0, len(lines) - 2, 3):
                sha, requested, effective = lines[index:index+3]
                if sha == bundle_sha and requested in ('ssh', 'tools', 'full') and effective in ('ssh', 'tools', 'full'):
                    if effective == 'full' and not self.installed(bundle_sha):
                        return False
                    return effective
        except (OSError, EOFError, RuntimeError, paramiko.SSHException):
            pass
        return False

    def installed(self, bundle_sha):
        import paramiko
        try:
            result = self.command('test ! -e /root/.zigma-install/pending && '
                'cat /root/zigma-free/installer-bundle.sha256 && '
                'cat /root/.zigma-install/transaction-*/completed-profile')
            lines = result.splitlines()
            match = bool(lines and lines[0] == bundle_sha and 'full' in lines[1:])
            trace(f'[INSTALL] No pending transaction; expected bundle SHA256={bundle_sha}; hash match={bool(lines and lines[0] == bundle_sha)}; full marker={"full" in lines[1:]}; result={"PASS" if match else "NOT READY"}.')
            return match
        except (OSError, EOFError, RuntimeError, paramiko.SSHException) as exc:
            trace(f'[INSTALL] Completion check not ready: {type(exc).__name__}. Will retry read-only checks.')
            return False


def activate(robot, allow_manual_reboot=True):
    import paramiko
    boot_before = robot.command('cat /proc/sys/kernel/random/boot_id', timeout=5)
    print('Starting the 240-second local-mode trial; checking SSH and web automatically.')
    # Never repeat a mutating command after an ambiguous network failure.
    try:
        robot.command('/root/zigma-free/activate start', timeout=25)
    except (OSError, EOFError, paramiko.SSHException):
        print('Activation response lost; checking state without sending start again.')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    url = 'http://' + robot.host + ':8765'
    from ssh_output import redact
    failures = []
    last_reason = None
    connection_lost = False
    def failed(stage, reason):
        nonlocal last_reason
        reason = redact(str(reason))[:1500]
        record = dict(at=datetime.datetime.now().astimezone().isoformat(), stage=stage, reason=reason)
        failures.append(record)
        if (stage, reason) != last_reason:
            print('[TRIAL] ' + stage + ': ' + reason, flush=True)
            last_reason = (stage, reason)
        return False
    def healthy():
        nonlocal connection_lost
        stage = 'SSH / activation state'
        connection_lost = False
        try:
            state = robot.command('/root/zigma-free/activate status', timeout=5)
            if not state.startswith('Trial pending;'):
                return failed(stage, 'Expected pending trial; received: ' + state)
            stage = 'Local control queue'
            robot.command('/root/bin/zigmactl check-control', timeout=5)
            stage = 'Web credential'
            token = robot.command('cat /root/zigma-free/data/access.token', timeout=5)
            if not token or '\n' in token:
                return failed(stage, 'Missing or malformed access token (value withheld)')
            stage = 'Authenticated web HTTP'
            request = urllib.request.Request(url + '/api/status', headers={'Authorization': 'Bearer ' + token})
            with opener.open(request, timeout=4) as response:
                stage = 'Web status JSON'
                status = json.loads(response.read(1024 * 1024))
                if response.status != 200 or not isinstance(status, dict) or not status:
                    return failed(stage, 'Expected HTTP 200 and nonempty JSON object')
            return token
        except (OSError, EOFError, ValueError, RuntimeError, paramiko.SSHException) as exc:
            connection_lost = (stage == 'SSH / activation state' and
                               isinstance(exc, (OSError, EOFError, paramiko.SSHException)) and
                               not isinstance(exc, paramiko.AuthenticationException))
            # Never include response bodies or credential-returning output.
            if stage in ('Web credential', 'Web status JSON'):
                return failed(stage, type(exc).__name__ + '; value/body withheld')
            return failed(stage, type(exc).__name__ + ': ' + str(exc))
    try:
        token = wait_until(healthy, 90, 'Local-mode verification failed; keep power on for automatic recovery.', label='Authenticated SSH/web trial', quiet=True)
    except TimeoutError as exc:
        detail = (last_reason[0] + ': ' + last_reason[1]) if last_reason else str(exc)
        saved = None
        try:
            now = datetime.datetime.now().astimezone()
            saved = robot.directory/('trial-failure-' + now.strftime('%Y%m%d-%H%M%S') + '.json')
            with saved.open('w', encoding='utf-8') as f:
                saved.chmod(0o600)
                json.dump(dict(at=now.isoformat(), host=robot.host, failures=failures), f, indent=2)
            print('[DIAGNOSTIC] Trial checks saved: ' + str(saved))
        except (OSError, TypeError) as error:
            print('[DIAGNOSTIC] Cannot save trial checks: ' + type(error).__name__)
        # Best effort, bounded, read-only: preserve the robot's state record too.
        try:
            robot.command('tail -c 65536 /root/zigma-free/activation/trial-diagnosis.log',
                          timeout=5, diagnostic='trial-state')
        except (OSError, EOFError, ValueError, RuntimeError, paramiko.SSHException) as error:
            print('[DIAGNOSTIC] Robot state capture currently unreachable: ' + type(error).__name__ +
                  '. It remains on the robot for collection after recovery.')
        if connection_lost and allow_manual_reboot:
            print('The robot is unreachable over SSH after 90 seconds. Its Wi-Fi LED does not prove LAN connectivity.')
            print('Restart the robot manually now. Leave this window open; waiting up to 10 minutes for its return. '
                  'The next boot cancels the unaccepted trial. No pairing or reinstall is needed.', flush=True)
            def returned():
                try:
                    value = robot.command('cat /proc/sys/kernel/random/boot_id; /root/zigma-free/activate status', timeout=5)
                    boot, state = value.split('\n', 1)
                    if state.strip() != 'Local mode not selected.':
                        return False
                    return 'rebooted' if boot != boot_before else 'automatic-recovery'
                except (OSError, EOFError, ValueError, RuntimeError, paramiko.SSHException):
                    return False
            recovery = wait_until(returned, 600, 'Robot did not return. Keep the saved session; do not resend pairing.',
                                  label='Manual reboot / automatic recovery', quiet=True)
            if recovery == 'rebooted':
                try:
                    robot.command('tail -c 65536 /root/zigma-free/activation/trial-diagnosis.log',
                                  timeout=5, diagnostic='before-retry-state')
                except (OSError, EOFError, ValueError, RuntimeError, paramiko.SSHException):
                    print('[DIAGNOSTIC] Previous robot state record could not be retrieved.')
                print('New boot and SSH verified. Retrying activation once; existing installation and backup retained.')
                return activate(robot, allow_manual_reboot=False)
            print('The robot recovered automatically without a reboot. Session retained; resume activation when ready.')
        raise TimeoutError('Local-mode verification failed. Last failed check: ' + detail +
                           '. Keep power on: the robot returns to vendor mode after the 240-second trial. '
                           'Do not repeat pairing or reinstall.' + ((' Report: ' + str(saved)) if saved else '')) from exc
    try:
        robot.command('/root/zigma-free/activate accept', timeout=20)
    except (OSError, EOFError, paramiko.SSHException):
        print('Acceptance response lost; checking status without repeating accept.')
    state = robot.command('/root/zigma-free/activate status')
    if not state.startswith('Local mode accepted;'):
        raise RuntimeError('Local mode was not confirmed accepted. Keep robot powered for rollback.')
    print('\nInstallation verified and local mode accepted.\nWeb: ' + url)
    open_robot_web(url, token)
    return url


def verify_release(root):
    sums = root/'SHA256SUMS'
    if not sums.is_file():
        raise ValueError('Use the extracted deployment package, containing SHA256SUMS.')
    trace(f'[INTEGRITY] Verifying release manifest: {sums}')
    for line in sums.read_text().splitlines():
        expected, name = line.split('  ', 1)
        path = (root/name).resolve()
        if root.resolve() not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError('Release checksum failed: ' + name)
        trace(f'[INTEGRITY] PASS {name}; SHA256={expected}; bytes={path.stat().st_size}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check prerequisites/network/package only; no robot access')
    parser.add_argument('--session', type=Path, help='Continue this saved session without pairing (otherwise show saved sessions)')
    parser.add_argument('--port', type=int, default=8000)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('Invalid port')
    if sys.version_info < (3, 10):
        raise ValueError('Python 3.10 or newer is required.')
    trace(f'[START] Verbose installer; Python={sys.version.split()[0]}; platform={sys.platform}; executable={sys.executable}.')
    trace('[START] Every Python output line includes local date/time/timezone. Wi-Fi password is visible while typing and shown after pairing; the web token is shown before opening the browser. Private keys stay hidden.')
    dependencies()
    verify_release(Path(__file__).resolve().parents[1])
    choices = networks()
    if len(choices) != 1:
        raise ValueError('Start on your HOME Wi-Fi, not the robot pairing Wi-Fi (192.168.99.x). We need the computer HOME IP first so the robot knows where to download the files after joining your network. Connect to home Wi-Fi and restart; found ' + str(len(choices)) + ' usable home Wi-Fi addresses (exactly one is required).')
    network = choices[0]
    print('Home Wi-Fi: ' + network.ssid + '; interface: ' + network.interface + '; IPv4: ' + network.host)
    if args.check:
        print('Prerequisites and release checks passed. No robot contacted or Wi-Fi changed.')
        return
    session = args.session
    if session is None:
        saved_sessions = sorted(p.parent for p in (Path.home()/'Zigma').glob('*/connection.txt'))
        if saved_sessions:
            print('0: New installation')
            for number, path in enumerate(saved_sessions, 1):
                print(f'{number}: Continue saved session {path.name}')
            print('Choose a number. Continue an existing robot instead of pairing it again:', flush=True)
            choice = int(input())
            if not 0 <= choice <= len(saved_sessions):
                raise ValueError('Invalid session selection')
            if choice:
                session = saved_sessions[choice - 1]
    if session is not None:
        from session_recovery import continue_session
        continue_session(session, network, args.port)
        return
    print('The saved Wi-Fi profile will be used to reconnect automatically.')
    print('Keep the robot stationary. This installs full Zigma Free and accepts local mode after SSH/web checks.')
    print('First SSH host key is trusted on this local network and pinned for the session.')
    print('Allow Python inbound TCP ' + str(args.port) + ' on your home network in the firewall.')
    print('Private/guest networks with client isolation are not supported.')
    print('Robot name [Vacuum]:', flush=True)
    name = input().strip() or 'Vacuum'
    if not __import__('re').fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,39}', name):
        raise ValueError('Name: 1-40 letters, digits, underscores or hyphens; start with a letter/digit.')
    robot_ssid = choose_robot_wifi(network)
    password = read_wifi_password(robot_ssid)
    if any(c in password for c in '\"\\\r\n'):
        raise ValueError('Vendor pairing cannot safely encode quotes, backslashes or newlines in Wi-Fi credentials.')
    directory = Path.home()/'Zigma'/(name + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    private_directory(directory)
    import paramiko
    trace('[KEYS] Generating ECDSA P-256 SSH key pair; private key will never be printed.')
    key = paramiko.ECDSAKey.generate(bits=256)
    key.write_private_key_file(str(directory/'zigma_key'))
    (directory/'zigma_key').chmod(0o600)
    public = key.get_name() + ' ' + key.get_base64()
    (directory/'zigma_key.pub').write_text(public + '\n', encoding='ascii')
    print('Created your ECDSA P-256 key pair. Keep the PRIVATE file safe; it has no passphrase.')
    print('Private key: ' + str(directory/'zigma_key') + '\nPublic key: ' + str(directory/'zigma_key.pub'))
    print(public)
    base = f'http://{network.host}:{args.port}'
    trace(f'[BUILD] Preparing full profile with Telegram transport; robot={name}; download base={base}.')
    full_script, bundle, plan = installer.build('full', public, base, name, True)
    script, _, ssh_plan = installer.build('ssh', public, base, name, False)
    (directory/'full-install.sh').write_bytes(full_script)
    (directory/'install.sh').write_bytes(script)
    (directory/'bundle.tar.gz').write_bytes(bundle)
    (directory/'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    trace(f'[BUILD] Saved script={len(script)} bytes; bundle={len(bundle)} bytes; bundle SHA256={plan["bundle_sha256"]}.')
    events = queue.Queue()
    subnet = ipaddress.ip_network(f'{network.host}/{network.prefix}', strict=False)
    handler = observed_handler({'/install.sh': script, '/bundle.tar.gz': bundle}, events, subnet)
    # Wildcard bind survives losing the home IP during the AP visit.
    with installer.InstallerServer(('0.0.0.0', args.port), handler, tls_context(directory, network.host)) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        trace(f'[SERVER] Background listener started on 0.0.0.0:{args.port}; HTTP+TLS; two allowlisted assets; home subnet={subnet}.')
        try:
            local = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                urllib.request.HTTPSHandler(context=ssl._create_unverified_context()))
            for scheme in ('http', 'https'):
                trace(f'[SELFTEST] Requesting exact install.sh bytes over {scheme.upper()}.')
                with local.open(f'{scheme}://{network.host}:{args.port}/install.sh', timeout=5) as response:
                    if response.read() != script:
                        raise ValueError('Local HTTP/TLS server self-test failed')
                trace(f'[SELFTEST] {scheme.upper()} PASS. External firewall reachability still requires robot download.')
            print('Server ready. Waiting up to 10 minutes; the next step starts automatically when the robot AP is reachable.')
            print('NOW HOLD THE ROBOT POWER BUTTON FOR 10 SECONDS to enter pairing, then connect this computer to the ROBOT Wi-Fi.', flush=True)
            wait_until(ready_ap, 600, 'Robot AP not found. No pairing request sent.', label='Robot AP TCP service', quiet=True)
            from hosts_guard import before_pairing
            before_pairing(directory)
            config = send_pairing.build_config(robot_ssid, password, 'Redmond', '0086',
                'http://127.0.0.1/', base + '/install.sh', hashlib.md5(script).hexdigest())
            started = time.monotonic()
            print('Sending pairing ONCE (Redmond), then returning to home Wi-Fi immediately.')
            try:
                post_pairing(config)
                print('Robot accepted pairing.')
            except (OSError, ValueError) as exc:
                print('Pairing response uncertain (' + type(exc).__name__ + '); request will NOT be repeated.')
            finally:
                show_sent_wifi(robot_ssid, password)
                config.clear()
                password = None
                try:
                    network.reconnect()
                except (OSError, subprocess.SubprocessError):
                    print('Automatic reconnect failed. Select your home Wi-Fi NOW in system settings.')
            wait_until(lambda: restored(network), 45,
                'Home Wi-Fi/original IP not restored. Reconnect manually; do not resend pairing blindly. '
                'Session files are saved; see troubleshooting in README.md.', label='Home Wi-Fi and original IPv4', quiet=True)
            print(f'Home Wi-Fi restored after {time.monotonic() - started:.1f}s.')
            print('PLEASE BE PATIENT: allow about 60 seconds for the robot to follow you home and start downloading (63-75s observed after pairing). Keep this window open; no action is needed.', flush=True)
            seen = {}
            def downloads():
                while True:
                    try:
                        peer, asset = events.get_nowait()
                    except queue.Empty:
                        break
                    if peer != network.host:
                        seen.setdefault(peer, set()).add(asset)
                        trace(f'[DISCOVERY] Peer={peer}; completed assets={sorted(seen[peer])}; elapsed since pairing={time.monotonic() - started:.1f}s.')
                candidates = [ip for ip, paths in seen.items() if '/install.sh' in paths]
                if len(candidates) > 1:
                    raise ValueError('Multiple download clients; cannot safely identify the robot.')
                return candidates[0] if candidates else None
            host = wait_until(downloads, 240,
                'No robot script download. Check firewall/client isolation and original PC IP. Do not resend blindly.', label='Robot entry-script download', quiet=True)
            print('Robot downloaded the entry script from IP ' + host + '. Waiting for SSH and the actual installation result ...')
            robot = Robot(host, key, directory)
            effective = wait_until(lambda: robot.completed_profile(ssh_plan['bundle_sha256']), 240,
                       'Installation completion not verified. Local mode was NOT activated; retain session for diagnosis.', label='Session installation completion', quiet=True)
            fingerprint = base64.b64encode(hashlib.sha256(robot.host_key.asbytes()).digest()).decode().rstrip('=')
            print('Pinned robot SSH fingerprint: SHA256:' + fingerprint)
            (directory/'connection.txt').write_text('SSH: root@' + host + '\nPrivate key: ' + str(directory/'zigma_key') + '\n', encoding='utf-8')
            from hosts_guard import apply as apply_hosts
            apply_hosts(robot)
            from nor_backup import download_with_choice, connect, upload
            backup = download_with_choice(robot, installer.ROBOT/'build/free-zigma-agent-arm')
            print(('Verified NOR backup saved. ' if backup else 'NO VERIFIED BACKUP: risk explicitly accepted. ') +
                  'Checking firmware compatibility and installing full ...')
            with connect(robot) as client:
                stage = robot.command('mktemp -d /tmp/zigma-full.XXXXXX')
                if not __import__('re').fullmatch(r'/tmp/zigma-full\.[A-Za-z0-9]+', stage):
                    raise ValueError('Invalid temporary installation directory')
                try:
                    upload(client, stage + '/install.sh', full_script)
                    expected = hashlib.sha256(full_script).hexdigest()
                    robot.command('test "$(sha256sum ' + stage + '/install.sh | cut -d " " -f 1)" = ' + expected + ' && sh ' + stage + '/install.sh', timeout=240, diagnostic='full-install')
                finally:
                    try:
                        robot.command('rm -f ' + stage + '/install.sh; rmdir ' + stage)
                    except Exception as cleanup_error:
                        print('[CLEANUP] Temporary installer cleanup failed: ' + str(cleanup_error))
            effective = wait_until(lambda: robot.completed_profile(plan['bundle_sha256']), 60,
                'Full completion was not verified; local mode was NOT activated. Backup and SSH are retained.', label='Full installation result', quiet=True)
            if effective != 'full':
                (directory/'connection.txt').write_text('SSH: root@' + host + '\nPrivate key: ' + str(directory/'zigma_key') + '\n', encoding='utf-8')
                print('SSH access installed and boot startup configured. Full Zigma Free was NOT installed or activated because firmware compatibility needs review.')
                print('Your key is saved in ' + str(directory) + '. Check the session backup status before relying on it for recovery. Cold-boot persistence still needs a manual test.')
                return
            url = activate(robot)
            (directory/'connection.txt').write_text('Web: ' + url + '\nSSH: root@' + host +
                '\nPrivate key: ' + str(directory/'zigma_key') + '\n', encoding='utf-8')
            print('Credentials folder: ' + str(directory))
            print('Set your timezone in Settings. After acceptance, test a physical power cycle manually; persistence is not yet verified.')
        finally:
            trace('[SERVER] Stopping listener and joining background thread.')
            server.shutdown()
            worker.join(timeout=5)
            trace('[SERVER] Listener stopped; session files and keys retained.')


if __name__ == '__main__':
    from console_output import configure_output
    configure_output()
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped. Do not repeat pairing blindly. A pending trial rolls back after 240 seconds; keep robot powered.')
        raise SystemExit(130)
    except Exception as exc:
        # No raw subprocess output or HTTP bodies: they may contain credentials.
        if isinstance(exc, subprocess.SubprocessError):
            print('ERROR: A required system command failed. Check Wi-Fi permissions/NetworkManager and saved profile.', file=sys.stderr)
        else:
            print('ERROR: ' + str(exc), file=sys.stderr)
        raise SystemExit(1)
