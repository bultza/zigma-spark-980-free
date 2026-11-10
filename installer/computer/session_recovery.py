"""Resume only a completed SSH stage with the saved, verified pre-full backup."""
import hashlib
import ipaddress
import json
from pathlib import Path
import queue
import re
import shlex
import threading
import datetime
import paramiko
import installer
from nor_backup import connect, upload

# Lock on the inherited open-file description, never delete the lock file.
# -n fails if a different installer owns it. Unlock only after checking markers
# under that lock; this also releases the stale shared description in Dropbear.
RELEASE_LOCK = '''set -eu
lock=/root/.zigma-install/lock
if test "$(readlink /proc/$$/fd/9 2>/dev/null || :)" != "$lock"; then
 exec 9<"$lock"
fi
flock -n 9 || { echo 'ERROR: A different installation still owns the lock' >&2; exit 1; }
test ! -e /root/.zigma-install/pending || { echo 'ERROR: Pending transaction; refusing resume' >&2; exit 1; }
test ! -e /root/zigma-free/installer-bundle.sha256 || { echo 'ERROR: Full already present; refusing resume' >&2; exit 1; }
flock -u 9
exec 9>&-
echo 'Completed SSH-stage lock released; no service stopped or rebooted'
'''


def verified_backup(session, host):
    manifest=json.loads((session/'nor-backup.json').read_text())
    if manifest.get('verified') is not True or manifest.get('size')!=33554432 or manifest.get('robot_host')!=host:
        raise ValueError('Missing verified NOR backup for this saved robot; resume refused')
    path=session/'nor-backup.bin'
    if path.stat().st_size!=33554432:
        raise ValueError('NOR backup has an incorrect size')
    digest=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(65536),b''):digest.update(block)
    if digest.hexdigest()!=manifest.get('first_sha256') or digest.hexdigest()!=manifest.get('second_sha256'):
        raise ValueError('Saved NOR backup hash mismatch; resume refused')
    return digest.hexdigest()


def continue_session(session, network, port):
    import wizard
    if session is None:
        candidates=sorted(p.parent for p in (Path.home()/'Zigma').glob('*/nor-backup.json') if (p.parent/'connection.txt').exists())
        if not candidates:raise ValueError('No saved session with NOR backup; use --session PATH')
        for i,p in enumerate(candidates,1):print(f'{i}: {p}')
        if len(candidates)==1:session=candidates[0]
        else:
            print('Choose the SSH-only session to resume (number):',flush=True)
            choice=int(input())
            if not 1<=choice<=len(candidates):raise ValueError('Invalid session selection')
            session=candidates[choice-1]
    session=Path(session)
    match=re.search(r'^SSH: root@([0-9.]+)$',(session/'connection.txt').read_text(),re.M)
    if not match:raise ValueError('No saved robot address')
    host=str(ipaddress.IPv4Address(match[1]))
    print('Continue session: '+str(session)+'; robot='+host+'. No pairing or Wi-Fi password request.')
    key=paramiko.ECDSAKey.from_private_key_file(str(session/'zigma_key'))
    known=paramiko.HostKeys(str(session/'known_hosts'))
    if host not in known or len(known[host])!=1:raise ValueError('A unique saved SSH host key is required')
    robot=wizard.Robot(host,key,session);robot.host_key=next(iter(known[host].values()))
    public=key.get_name()+' '+key.get_base64()
    saved=json.loads((session/'plan.json').read_text())
    if saved.get('profile')!='full':raise ValueError('Saved session was not a full installation')
    name=saved.get('robot_name','')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,39}',name):raise ValueError('Invalid saved robot name')
    from hosts_guard import apply as apply_hosts
    apply_hosts(robot)
    try:
        print('Saved NOR backup reverified: ' + verified_backup(session, host))
    except (OSError, ValueError) as backup_error:
        print('No valid saved backup: ' + str(backup_error))
        from nor_backup import download_with_choice
        download_with_choice(robot, installer.ROBOT/'build/free-zigma-agent-arm')
    # A failed activation does not require installing the payload again.
    if robot.completed_profile(saved.get('bundle_sha256', '')) == 'full':
        state = robot.command('/root/zigma-free/activate status', timeout=5)
        if state.startswith('Local mode accepted;'):
            print('This session is already installed and accepted. Hosts protection checked; no payload reinstalled.')
            return
        if state.startswith('Trial pending;'):
            raise ValueError('The previous trial is still pending; keep power on and let it recover before resuming')
        if state.strip() != 'Local mode not selected.':
            raise ValueError('Unrecognized activation state; no change made')
        print('Full already installed for this session. Retrying activation only; no pairing or payload upload.')
        wizard.activate(robot)
        return
    base=f'http://{network.host}:{port}'
    script,bundle,plan=installer.build('full',public,base,name,bool(saved.get('telegram')))
    if plan['bundle_sha256']!=saved.get('bundle_sha256'):
        raise ValueError('Robot payload differs from saved session; this resume is not an updater')
    _,_,ssh_plan=installer.build('ssh',public,base,name,False)
    if robot.completed_profile(ssh_plan['bundle_sha256'])!='ssh':
        raise ValueError('A completed SSH-only stage without pending transaction is required')
    # No change has happened to the robot above this point.
    run=session/('resume-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    wizard.private_directory(run)
    (run/'full-install.sh').write_bytes(script)
    (run/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    subnet=ipaddress.ip_network(f'{network.host}/{network.prefix}',strict=False)
    handler=wizard.observed_handler({'/bundle.tar.gz':bundle},queue.Queue(),subnet)
    with installer.InstallerServer(('0.0.0.0',port),handler,wizard.tls_context(run,network.host)) as server:
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        try:
            robot.command(RELEASE_LOCK,diagnostic='resume-lock')
            with connect(robot) as client:
                stage=robot.command('mktemp -d /tmp/zigma-full.XXXXXX')
                if not re.fullmatch(r'/tmp/zigma-full\.[A-Za-z0-9]+',stage):raise ValueError('Invalid staging directory')
                try:
                    upload(client,stage+'/install.sh',script)
                    expected=hashlib.sha256(script).hexdigest()
                    robot.command('test "$(sha256sum '+stage+'/install.sh | cut -d " " -f 1)" = '+expected+' && sh '+stage+'/install.sh',timeout=240,diagnostic='full-install')
                finally:
                    try:robot.command('rm -f '+stage+'/install.sh; rmdir '+stage)
                    except Exception as exc:print('[CLEANUP] '+str(exc))
            effective=wizard.wait_until(lambda:robot.completed_profile(plan['bundle_sha256']),60,'Full completion not confirmed; local mode was not activated',label='Continued full installation',quiet=True)
            if effective!='full':
                print('Firmware requires review. SSH and the existing verified NOR backup are retained; full was not activated.')
                return
            url=wizard.activate(robot)
            (session/'connection.txt').write_text('Web: '+url+'\nSSH: root@'+host+'\nPrivate key: '+str(session/'zigma_key')+'\n')
            print('Installation completed. Original key and NOR backup retained: '+str(session))
        finally:
            server.shutdown();worker.join(timeout=5)
