"""Apply reviewed hosts overrides over authenticated, pinned SSH."""
from functools import wraps
import hashlib
from pathlib import Path
import re
import socket
from nor_backup import connect, upload


def best_effort(operation):
    @wraps(operation)
    def attempt(*args, **kwargs):
        try:
            return operation(*args, **kwargs)
        except Exception as exc:
            # Keep credentials out of warnings; user cancellation still propagates.
            print('[HOSTS] WARNING: Protection could not be verified (' +
                  type(exc).__name__ + '); continuing installation.', flush=True)
            return False
    return attempt


@best_effort
def apply(robot):
    script = (Path(__file__).resolve().parent.parent / 'robot' / 'vendor-hosts.sh').read_bytes()
    digest = hashlib.sha256(script).hexdigest()
    with connect(robot) as client:
        stage = robot.command('mktemp -d /tmp/zigma-hosts-pc.XXXXXX')
        if not re.fullmatch(r'/tmp/zigma-hosts-pc\.[A-Za-z0-9]+', stage):
            raise ValueError('Invalid hosts staging path')
        try:
            upload(client, stage+'/apply.sh', script)
            robot.command('test "$(sha256sum '+stage+'/apply.sh | cut -d " " -f 1)" = '+digest+
                          ' && sh '+stage+'/apply.sh', timeout=30, diagnostic='vendor-hosts')
            original = robot.command('cat /root/.zigma-hosts/hosts.original')
            path = robot.directory/'hosts.original.txt'
            if not path.exists():
                path.write_text(original+'\n', encoding='utf-8');path.chmod(0o600)
        finally:
            try:robot.command('rm -f '+stage+'/apply.sh; rmdir '+stage)
            except Exception as exc:print('[HOSTS] Staging cleanup failed: '+type(exc).__name__)

    return True


@best_effort
def before_pairing(directory):
    """Only use a saved key after matching its pinned host key, never guess identity."""
    import paramiko
    import wizard
    try:
        sock=socket.create_connection(('192.168.99.1',22),timeout=2)
        with paramiko.Transport(sock) as transport:
            transport.start_client(timeout=4)
            host_key=transport.get_remote_server_key()
    except (OSError, EOFError, paramiko.SSHException):
        print('[HOSTS] No SSH in pairing. The first home-Wi-Fi connection is not protected; '
              'the downloaded entry script will attempt to apply hosts before recording installation completion.')
        return False
    candidates=[]
    for path in sorted((Path.home()/'Zigma').glob('*/known_hosts')):
        try:
            known=paramiko.HostKeys(str(path))
            if (path.parent/'zigma_key').is_file() and any(
                    key==host_key for entry in known.values() for key in entry.values()):
                candidates.append(path.parent)
        except (OSError, ValueError, paramiko.SSHException):
            continue
    if not candidates:
        print('[HOSTS] Pairing SSH is reachable, but no matching saved host pin/key was found. '
              'No untrusted key will be used. Initial home-Wi-Fi connection is unprotected; '
              'the downloaded entry script will attempt to apply hosts before recording installation completion.')
        return False
    if len(candidates)==1:
        session=candidates[0]
    else:
        for n,path in enumerate(candidates,1):print(f'{n}: Saved SSH session {path.name}')
        print('Choose the matching saved SSH session for hosts protection before pairing:',flush=True)
        choice=int(input())
        if not 1<=choice<=len(candidates):raise ValueError('Invalid saved SSH session')
        session=candidates[choice-1]
    key=paramiko.ECDSAKey.from_private_key_file(str(session/'zigma_key'))
    output=directory/'ap-hosts';wizard.private_directory(output)
    robot=wizard.Robot('192.168.99.1',key,output);robot.host_key=host_key
    # Hosts authentication failures are advisory; no untrusted key is accepted.
    if not apply(robot):
        return False
    print('[HOSTS] Applied and checked over pinned SSH while still in pairing, BEFORE Wi-Fi credentials are sent.')
    return True
