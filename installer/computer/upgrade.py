"""SSH-only managed-runtime and boot-timezone upgrades, with independent rollback and private PC backups."""
import base64
import datetime
import getpass
import gzip
import hashlib
import ipaddress
import io
import tarfile
import json
from pathlib import Path
import re
import shlex
import time
import urllib.request
import uuid

import paramiko
import installer
from nor_backup import connect, upload

BASE = '/root/zigma-free'
BINARY_NAMES = ('free-zigma-agent-arm', 'zigma-observe-arm', 'zigma-core-arm',
                'zigma-network-arm', 'zigma-area-arm', 'zigma-zones-arm',
                'zigma-maps-arm', 'zigma-options-arm', 'zigma-curl-arm', 'zigmactl')
WEB_NAMES = ('app.js', 'index.html', 'style.css', 'nor.js', 'favicon.svg')
# Only package-owned runtime files. Device records, credentials, jobs, vendor
# wrappers and boot hooks are preserved (timezone hooks have their own migration).
SCRIPT_NAMES = ('zigma-mode', 'zigma-mode-backend', 'zigma-free-service',
                'zigma-cron', 'zigma-settings', 'zigma-time-sync')
SCRIPT_FILES = {BASE+'/'+name: 'stack/'+name for name in SCRIPT_NAMES
                if name != 'zigma-time-sync'}
SCRIPT_FILES.update({'/root/bin/zigma-time-sync': 'support/zigma-time-sync',
                     BASE+'/activate': 'stack/activate',
                     BASE+'/firmware-profile.sh': 'stack/firmware-profile.sh'})
# Pre-modular installations lack these helpers; both are safe to add without
# invoking activation or changing the selected firmware.
LEGACY_HELPERS = ('activate', 'firmware-profile.sh')
TRANSPORT_FILES = ('telegram-ca.pem', 'licenses/transport.txt')
INVENTORY_NAMES = BINARY_NAMES + SCRIPT_NAMES



TIMEZONE_INIT = '/root/etc/init.d/S04zigma-timezone'
NAV_INIT = '/root/etc/init.d/S51hoslam'
TIMEZONE_MARKER = b'# zigma-selected-timezone-v1'
TIMEZONE_BLOCK = (TIMEZONE_MARKER + b'\n'
                  b'if [ -f /root/etc/localtime ]; then TZ=:/root/etc/localtime; export TZ; fi\n')


def navigation_timezone(data):
    """Preserve every vendor byte except the reviewed post-shebang insertion."""
    if not data.startswith(b'#!/bin/sh\n'):
        raise ValueError('Unsupported navigation startup script')
    body = data[len(b'#!/bin/sh\n'):]
    if body.startswith(TIMEZONE_BLOCK):
        if TIMEZONE_MARKER in body[len(TIMEZONE_BLOCK):]:
            raise ValueError('Duplicate navigation timezone marker')
        return data
    if TIMEZONE_MARKER in body:
        raise ValueError('Unrecognized navigation timezone edit')
    return b'#!/bin/sh\n' + TIMEZONE_BLOCK + body


def timezone_payload(robot):
    # read_file rejects links; the transaction rechecks these bytes before writing.
    data, mode = read_file(robot, NAV_INIT)
    boot = (installer.ROBOT/'init/S04zigma-timezone').read_bytes()
    return {TIMEZONE_INIT: (boot, 0o700),
            NAV_INIT: (navigation_timezone(data), int(mode, 8))}, {NAV_INIT: data}


def saved_connection(session):
    text = (session/'connection.txt').read_text()
    match = re.search(r'^SSH: root@([0-9.]+)$', text, re.M)
    if not match:
        raise ValueError('Session has no saved IPv4 address')
    host = str(ipaddress.IPv4Address(match[1]))
    key = session/'zigma_key'
    if not key.is_file():
        match = re.search(r'^Private key: (.+)$', text, re.M)
        if not match:
            raise ValueError('Session has no private key path')
        key = Path(match[1]).expanduser()
    known = paramiko.HostKeys(str(session/'known_hosts'))
    if host not in known or len(known[host]) != 1:
        raise ValueError('Session needs exactly one host key for its saved IP')
    return host, key, next(iter(known[host].values()))


def load_key(path):
    password = None
    for attempt in range(2):
        for kind in (paramiko.ECDSAKey, paramiko.RSAKey, paramiko.Ed25519Key):
            try:
                return kind.from_private_key_file(str(path), password=password)
            except paramiko.PasswordRequiredException:
                break
            except paramiko.SSHException:
                continue
        else:
            raise ValueError('Unsupported private key or incorrect passphrase')
        password = getpass.getpass('Private key passphrase: ')
    raise ValueError('Cannot decrypt private key')


def select_connection(session):
    if session is None:
        sessions = sorted(p.parent for p in (Path.home()/'Zigma').glob('*/connection.txt'))
        print('0: Upgrade without a previous session')
        print('P: Select a session folder by path')
        for i, path in enumerate(sessions, 1):
            print(f'{i}: {path.name}')
        answer = input('Session [0]: ').strip() or '0'
        if answer.lower() == 'p':
            session = Path(input('Session folder: ').strip().strip('"')).expanduser()
        else:
            choice = int(answer)
            if not 0 <= choice <= len(sessions):
                raise ValueError('Invalid session selection')
            session = sessions[choice-1] if choice else None
    previous, pin = '', None
    if session is not None:
        previous, path, pin = saved_connection(Path(session).expanduser())
    host = str(ipaddress.IPv4Address(input(f'Robot IP [{previous}]: ').strip() or previous))
    if session is None:
        path = Path(input('Private key path: ').strip().strip('"')).expanduser()
    path = path.resolve(strict=True)
    return host, path, pin


# Only transport failures are retryable; authentication and host identity stay fatal.
TRANSPORT_ERRORS = (OSError, EOFError, paramiko.SSHException)


def reconnect_command(robot, command, deadline, **kwargs):
    while True:
        try:
            return robot.command(command, **kwargs)
        except (paramiko.AuthenticationException, paramiko.BadHostKeyException):
            raise
        except TRANSPORT_ERRORS:
            if time.monotonic() >= deadline:
                raise
            print('SSH temporarily unavailable; waiting for the robot to reconnect.')
            time.sleep(min(3, max(0, deadline-time.monotonic())))


def transaction_state(robot, stage, deadline):
    return reconnect_command(robot, 'cat '+stage+'/state 2>/dev/null || echo starting', deadline)


def read_bytes(robot, command, limit=16*1024*1024):
    with connect(robot) as client:
        stdin, stdout, stderr = client.exec_command(command, timeout=60)
        stdin.close()
        data = stdout.read(limit+1)
        if len(data) > limit or stdout.channel.recv_exit_status() != 0:
            raise ValueError('Cannot read bounded remote file')
        return data


def read_file(robot, path):
    """Read bytes and original permissions together using the firmware's tar."""
    relative = path.lstrip('/')
    archive = read_bytes(robot, 'tar -cf - -C / '+shlex.quote(relative))
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:') as tar:
        members = tar.getmembers()
        if len(members) != 1 or members[0].name != relative or not members[0].isfile():
            raise ValueError('Expected one regular file in backup: '+path)
        member = members[0]
        with tar.extractfile(member) as stream:
            data = stream.read()
        if len(data) != member.size:
            raise ValueError('Truncated backup: '+path)
        return data, format(member.mode & 0o7777, 'o')


def api(robot, endpoint):
    token = read_bytes(robot, 'cat '+BASE+'/data/access.token', 4096).decode().strip()
    request = urllib.request.Request('http://'+robot.host+':8765/api/'+endpoint,
                                     headers={'Authorization': 'Bearer '+token})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=5) as response:
        return json.load(response)


def idle(robot):
    status = api(robot, 'status')
    if status.get('working_status') not in ('Standby', 'PileCharging', 'DirCharging', 'Hibernating') or status.get('error') != 'NoError':
        raise ValueError('Robot must be idle or charging without an error before upgrading; '
                         f"status={status.get('working_status', 'unknown')}, error={status.get('error', 'unknown')}")


def firmware_gate():
    # Managed installs retain the original init files in vendor-init.
    checks = installer.firmware_checks().replace('value=$(sha256sum "$1"', '''case "$1" in
 /etc/inittab) set -- /root/zigma-free/vendor-init/inittab.original;;
 /root/etc/init.d/S70yg) set -- /root/zigma-free/vendor-init/S70yg;;
 /root/etc/init.d/S99daemon) set -- /root/zigma-free/vendor-init/S99daemon;;
 esac
 value=$(sha256sum "$1"''')
    # Strip only our exact preamble for the vendor digest/calibration checks.
    checks = checks.replace('value=$(sha256sum "$1"',
                            'value=$(upgrade_firmware_bytes "$1" | sha256sum')
    checks = checks.replace("value=$(sed ", 'value=$(upgrade_navigation_original "$1" | sed ')
    checks = checks.replace("' \"$1\" | sha256sum)", "' | sha256sum)")
    return '''set -eu
upgrade_navigation_original() {
 test -f "$1" && test ! -L "$1" || return 1
 if test "$(sed -n '2p' "$1")" = '# zigma-selected-timezone-v1'; then
  test "$(sed -n '3p' "$1")" = 'if [ -f /root/etc/localtime ]; then TZ=:/root/etc/localtime; export TZ; fi' || return 1
  head -n 1 "$1"
  tail -n +4 "$1"
 else cat "$1"; fi
}
upgrade_firmware_bytes() {
 case "$1" in /root/etc/init.d/S51hoslam) upgrade_navigation_original "$1";; *) cat "$1";; esac
}
profile=full
requested_profile=full
layout=$(awk 'NR>1 {gsub(/"/,"",$4); printf "%s:%s;",$4,$2}' /proc/mtd)
case "$layout" in
 'uboot:00100000;dtb:00010000;kernel:00300000;rootfs:00600000;data:015f0000;') layout=direct;;
 'uboot:00050000;dtb:00010000;kernel:00300000;rootfs:00800000;data:004a0000;firmware:01000000;') layout=overlay;;
 *) exit 1;;
esac
''' + checks + '\ntest "$firmware_bad" = 0\n'


def payload(old):
    """Update the explicit managed-runtime allowlist and its inventory together."""
    # Preserve the original optional Telegram selection. Absence from the
    # inventory is legitimate for Full installed without --telegram.
    telegram = any(row.get('name') == 'zigma-curl-arm' for row in old['items'])
    full = installer.files_for('full', telegram=telegram)
    incoming = json.loads(full['stack/release.json'][0])['items']
    rows = {row['name']: row for row in incoming if row['name'] in INVENTORY_NAMES}
    previous = {row['name']: row for row in old['items']}
    if len(previous) != len(old['items']):
        raise ValueError('Duplicate installed component inventory')
    files = {}
    for name in INVENTORY_NAMES:
        if name == 'zigma-curl-arm' and not telegram:
            continue
        target = '/root/bin/'+name if name in ('zigmactl', 'zigma-time-sync') else BASE+'/'+name
        kind = 'binary' if name in BINARY_NAMES else 'script'
        row, before = rows.get(name, {}), previous.get(name, {})
        if row.get('path') != target or row.get('kind') != kind or before.get('path') != target or before.get('kind') != kind:
            raise ValueError('Installed component inventory needs review: '+name)
        source = SCRIPT_FILES[target] if kind == 'script' else ('tools/zigmactl' if name == 'zigmactl' else 'stack/'+name)
        files[target] = full[source]
        if installer.digest(files[target][0]) != row['sha256']:
            raise ValueError('Package component checksum mismatch: '+name)
    for target, source in SCRIPT_FILES.items():
        files[target] = full[source]
    for name in TRANSPORT_FILES if telegram else ():
        files[BASE+'/'+name] = full['stack/'+name]
    for name in WEB_NAMES:
        files[BASE+'/src/'+name] = full['stack/src/'+name]
    old['items'] = [rows.get(row['name'], row) for row in old['items']]
    files[BASE+'/release.json'] = ((json.dumps(old, indent=2)+'\n').encode(), 0o600)
    return files


def verify_inventory(about, expected):
    rows = {row['name']: row for row in about['items']}
    for row in expected['items']:
        if row['name'] not in INVENTORY_NAMES:
            continue
        actual = rows.get(row['name'], {})
        if actual.get('sha256') != row['sha256'] or actual.get('status') != 'verified':
            raise ValueError('About inventory not yet verified: '+row['name'])


def upgrade(session=None):
    import wizard
    host, key_path, pin = select_connection(session)
    key = load_key(key_path)
    directory = Path.home()/'Zigma'/('upgrade-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])
    wizard.private_directory(directory)
    robot = wizard.Robot(host, key, directory)
    if pin is None:
        # Verify first-use host identity before authenticating or executing commands.
        class Confirm(paramiko.MissingHostKeyPolicy):
            def missing_host_key(self, client, hostname, received):
                fingerprint = base64.b64encode(hashlib.sha256(received.asbytes()).digest()).decode().rstrip('=')
                print('SSH host fingerprint: SHA256:'+fingerprint)
                if input('Trust this robot host key? Type YES: ').strip() != 'YES':
                    raise ValueError('SSH identity not accepted')
                robot.host_key = received
        with paramiko.SSHClient() as client:
            client.set_missing_host_key_policy(Confirm())
            client.connect(host, username='root', pkey=key, look_for_keys=False,
                           allow_agent=False, timeout=8, auth_timeout=8, banner_timeout=8)
    else:
        robot.host_key = pin
    robot.command('test -f '+BASE+'/release.json && test -f '+BASE+'/local-mode.lock && '
                  'test ! -e /root/.zigma-install/pending && test ! -e '+BASE+'/activation/pending-boot')
    (directory/'connection.txt').write_text(f'SSH: root@{host}\nPrivate key: {key_path}\nWeb: http://{host}:8765\n')
    print('Upgrade session: '+str(directory)+'. Original session and private key are preserved.')
    timezone_files, timezone_originals = timezone_payload(robot)
    robot.command('sh -s', timeout=90, diagnostic='upgrade-firmware', script=firmware_gate())
    idle(robot)
    robot.command(BASE+'/zigma-free-service status && /root/bin/zigmactl check-control')
    boot = robot.command('cat /proc/sys/kernel/random/boot_id')
    old = json.loads(read_bytes(robot, 'cat '+BASE+'/release.json'))
    files = payload(old)
    files.update(timezone_files)
    backups, listing, changed = {}, [], {}
    # Keep only changed files; old bytes and permissions are verified again before replacement.
    for path, (data, mode) in files.items():
        q = shlex.quote(path)
        presence = robot.command('test ! -L '+q+' && { if test -f '+q+'; then echo present; '
                                 'elif test ! -e '+q+'; then echo absent; else exit 1; fi; }')
        index = str(len(listing))
        if presence == 'absent':
            if path not in {BASE+'/src/'+name for name in WEB_NAMES} | {TIMEZONE_INIT} | {BASE+'/'+name for name in TRANSPORT_FILES + LEGACY_HELPERS}:
                raise ValueError('Required installed component is missing: '+path)
            previous_mode, previous_sha = '-', 'absent'
        elif presence == 'present':
            before, previous_mode = read_file(robot, path)
            if path in timezone_originals and before != timezone_originals[path]:
                raise ValueError('Navigation startup changed during upgrade preparation')
            if path == TIMEZONE_INIT and before != data:
                raise ValueError('Unrecognized existing timezone boot script')
            if before == data:
                continue
            previous_sha = installer.digest(before)
            before_zip = gzip.compress(before, mtime=0)
            (directory/(index+'.old.gz')).write_bytes(before_zip)
            backups[index] = before_zip
        else:
            raise ValueError('Invalid file presence result: '+path)
        changed[index] = gzip.compress(data, mtime=0)
        listing.append(f'{index} {path} {previous_mode} {mode:o} {previous_sha} {installer.digest(data)}')
    if not listing:
        # Refresh the last successful updater label even for a no-op upgrade.
        # Its old contents are deliberately never read, compared or inventoried.
        version = shlex.quote(installer.package_version())
        robot.command('set -eu; t=$(mktemp '+BASE+'/.installer-version.XXXXXX); '
                      "printf '%s\\n' "+version+' >"$t"; chmod 644 "$t"; '
                      'mv -f "$t" '+BASE+'/installer-version.txt; sync')
        print('Managed runtime already matches this package. Installer/updater label refreshed.')
        return
    manifest = '\n'.join(listing)+'\n'
    (directory/'files.list').write_text(manifest)
    (directory/'boot').write_text(boot+'\n')
    print(f'{len(listing)} changed files; verified backups saved in {directory}.')
    needed = (sum(map(len, backups.values()))+sum(map(len, changed.values()))+max(len(v[0]) for v in files.values()))//1024+8192
    flash = max(len(v[0]) for v in files.values())//1024+2048
    robot.command(f'test "$(df -Pk /tmp | awk \'END {{print $4}}\')" -ge {needed} && '
                  f'test "$(df -Pk {BASE} | awk \'END {{print $4}}\')" -ge {flash} && '
                  f'test "$(df -Pk /root | awk \'END {{print $4}}\')" -ge {flash} && '
                  f'test "$(awk \'/^MemAvailable:/ {{a=$2}} /^MemFree:/ {{f=$2}} END {{print a ? a : f+0}}\' /proc/meminfo)" -ge {needed}')
    stage = robot.command('umask 077; mktemp -d /tmp/zigma-upgrade.XXXXXX')
    if not re.fullmatch(r'/tmp/zigma-upgrade\.[A-Za-z0-9]+', stage):
        raise ValueError('Invalid staging directory')
    script = (Path(__file__).with_name('upgrade-transaction.sh')).read_bytes()
    (directory/'upgrade-transaction.sh').write_bytes(script)
    assets = {'files.list': manifest.encode(), 'boot': (boot+'\n').encode(), 'transaction.sh': script,
              'installer-version.txt': (installer.package_version()+'\n').encode()}
    assets.update({i+'.old.gz': data for i, data in backups.items()})
    assets.update({i+'.new.gz': data for i, data in changed.items()})
    started = False
    try:
        with connect(robot) as client:
            for name, data in assets.items():
                upload(client, stage+'/'+name, data)
        sums = ''.join(installer.digest(data)+'  '+name+'\n' for name, data in assets.items())
        with connect(robot) as client:
            upload(client, stage+'/SHA256SUMS', sums.encode())
        robot.command('cd '+stage+' && sha256sum -c SHA256SUMS && sh -n transaction.sh', timeout=60)
        idle(robot)
        print('Installing. Keep the robot powered; disconnection triggers rollback after 240 seconds.')
        started = True  # Ambiguous SSH response must never cause a second transaction launch.
        deadline = time.monotonic()+180
        try:
            robot.command('nohup sh '+stage+'/transaction.sh '+stage+' run >'+stage+'/transaction.log 2>&1 </dev/null &', timeout=10)
        except (paramiko.AuthenticationException, paramiko.BadHostKeyException):
            raise
        except TRANSPORT_ERRORS:
            print('Launch response lost; checking the existing transaction without launching it again.')
        acceptance_attempted = False
        while time.monotonic() < deadline:
            state = transaction_state(robot, stage, deadline)
            if state in ('rolled-back', 'rollback-failed', 'failed'):
                raise ValueError('Upgrade transaction: '+state)
            if state == 'accepted' and acceptance_attempted:
                if reconnect_command(robot, 'test -f '+stage+'/done && echo done || echo waiting', deadline) != 'done':
                    time.sleep(1)
                    continue
                (directory/'verified.json').write_text(json.dumps({'host': host, 'boot': boot, 'files': listing}, indent=2)+'\n')
                print('Upgrade verified. Backups retained: '+str(directory))
                print('Managed scripts are updated; resident coordinator code takes effect on its next start. No mode switch was requested.')
                print('Boot timezone correction is installed. It takes full effect after the next robot reboot; no reboot was requested.')
                return
            if state == 'ready':
                try:
                    robot.command('test "$(cat /proc/sys/kernel/random/boot_id)" = '+shlex.quote(boot)+' && '+BASE+'/zigma-free-service status && '+
                                  BASE+'/zigma-core-arm --check-health /tmp/zigma-core/health.json --check-exe '+BASE+'/zigma-core-arm && '+
                                  BASE+'/zigma-core-arm --check-health /tmp/zigma-network/status.json --check-exe '+BASE+'/zigma-network-arm && /root/bin/zigmactl check-control')
                    idle(robot)
                    about = api(robot, 'about')
                    verify_inventory(about, old)
                    acceptance_attempted = True
                    robot.command('sh '+stage+'/transaction.sh '+stage+' accept', timeout=15)
                    for _ in range(15):
                        if robot.command('test -f '+stage+'/done && echo done || echo waiting') == 'done':
                            break
                        time.sleep(1)
                    else:
                        raise RuntimeError('Acceptance completion not confirmed')
                    (directory/'verified.json').write_text(json.dumps({'host': host, 'boot': boot, 'files': listing}, indent=2)+'\n')
                    print('Upgrade verified. Backups retained: '+str(directory))
                    print('Managed scripts are updated; resident coordinator code takes effect on its next start. No mode switch was requested.')
                    print('Boot timezone correction is installed. It takes full effect after the next robot reboot; no reboot was requested.')
                    return
                except (paramiko.AuthenticationException, paramiko.BadHostKeyException):
                    raise
                except (OSError, EOFError, ValueError, RuntimeError, paramiko.SSHException) as exc:
                    print('Verification not complete ('+type(exc).__name__+'); retrying within the upgrade deadline.')
            time.sleep(3)
        raise ValueError('Upgrade verification timed out')
    finally:
        if started:
            try:
                if (directory/'verified.json').exists():
                    try:
                        (directory/'transaction.log').write_bytes(read_bytes(robot, 'cat '+stage+'/transaction.log'))
                    except TRANSPORT_ERRORS:
                        print('Verified upgrade; transaction log could not be downloaded.')
                    robot.command('rm -rf '+stage)
                else:
                    deadline = time.monotonic()+270
                    reconnect_command(robot, 'touch '+stage+'/rollback.now', deadline)
                    print('Rollback requested. Keep power on; evidence retained at '+stage)
                    while time.monotonic() < deadline:
                        state = transaction_state(robot, stage, deadline)
                        if state in ('rolled-back', 'rollback-failed', 'failed', 'accepted'):
                            print('Final transaction state: '+state)
                            (directory/'transaction-state.txt').write_text(state+'\n')
                            (directory/'transaction.log').write_bytes(read_bytes(robot, 'cat '+stage+'/transaction.log'))
                            break
                        time.sleep(3)
                    else:
                        print('Rollback completion is not confirmed; retain the session and robot power.')
            except Exception:
                print('Transaction outcome could not be confirmed. Keep power on and retain the session: '+str(directory))
        else:
            robot.command('rm -rf '+stage)
