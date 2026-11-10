"""Bounded, two-pass NOR download. Unverified captures never count as verified backups."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex

SIZE = 32 * 1024 * 1024
BLOCK = 65536


def exact(stream, size):
    parts = bytearray()
    while len(parts) < size:
        data = stream.read(size - len(parts))
        if not data:
            raise ValueError('Truncated NOR stream; full installation blocked')
        parts.extend(data)
    return bytes(parts)


def capture(stream, destination, progress=print):
    header = json.loads(stream.readline(8193))
    if (header.get('format'), header.get('size'), header.get('block_size'), header.get('passes')) != ('zigma-nor-v1', SIZE, BLOCK, 2):
        raise ValueError('Unsupported NOR stream')
    first, second, blocks, changed = hashlib.sha256(), hashlib.sha256(), [], []
    with destination.open('xb') as output:
        for offset in range(0, SIZE, BLOCK):
            data = exact(stream, BLOCK)
            output.write(data)
            first.update(data)
            blocks.append(hashlib.sha256(data).hexdigest())
            if (offset + BLOCK) % (4*1024*1024) == 0:
                progress(f'NOR download: {(offset + BLOCK)//1048576}/32 MiB')
        output.flush()
        os.fsync(output.fileno())
    progress('NOR second full read: verifying every 64 KiB block ...')
    for offset in range(0, SIZE, BLOCK):
        data = exact(stream, BLOCK)
        second.update(data)
        if hashlib.sha256(data).hexdigest() != blocks[offset//BLOCK]:
            changed.append(offset//BLOCK)
        if (offset + BLOCK) % (4*1024*1024) == 0:
            progress(f'NOR verification: {(offset + BLOCK)//1048576}/32 MiB')
    result = json.loads(stream.readline(8193))
    if stream.read(1):
        raise ValueError('Unexpected NOR stream suffix')
    if changed or result.get('verified') is not True or result.get('first_sha256') != first.hexdigest() or result.get('second_sha256') != second.hexdigest() or first.digest() != second.digest():
        raise ValueError(f'NOR reads differ ({len(changed)} changed blocks); full installation blocked')
    # Check the saved file, too, rather than just bytes received over SSH.
    saved = hashlib.sha256()
    with destination.open('rb') as src:
        for data in iter(lambda: src.read(BLOCK), b''):
            saved.update(data)
    if saved.digest() != first.digest():
        raise ValueError('Saved NOR file checksum mismatch')
    return {**header, **result, 'block_sha256': blocks, 'captured_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'note': 'Captured after SSH installation. Two matching live reads are not an atomic filesystem snapshot. Contains private credentials.'}


def connect(robot):
    import paramiko
    client = paramiko.SSHClient()
    keys = client.get_host_keys()
    if robot.host_key is None:
        raise ValueError('SSH host key must be pinned before backup')
    keys.add(robot.host, robot.host_key.get_name(), robot.host_key)
    client.connect(robot.host, username='root', pkey=robot.key, allow_agent=False,
                   look_for_keys=False, timeout=8, banner_timeout=8, auth_timeout=8)
    return client


def upload(client, remote, data):
    # Dropbear firmware does not require an SFTP subsystem or an external scp.
    stdin, stdout, stderr = client.exec_command('umask 077; cat > ' + shlex.quote(remote), timeout=60)
    stdin.write(data)
    stdin.flush()
    stdin.channel.shutdown_write()
    stdout.read()
    if stdout.channel.recv_exit_status() != 0:
        raise RuntimeError('Failed to upload temporary helper')


def download(robot, helper):
    directory = robot.directory
    partial = directory/'nor-backup.bin.partial'
    binary = directory/'nor-backup.bin'
    manifest = directory/'nor-backup.json'
    if binary.exists() or manifest.exists():
        raise ValueError('Backup destination already exists; refusing to overwrite')
    data = Path(helper).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    remote = '/tmp/zigma-nor-' + digest[:16]
    print('SSH is ready. Downloading the complete NOR before installing full. Keep the robot idle and powered.')
    with connect(robot) as client:
        # Exclusive unpredictable directory prevents following stale paths/symlinks.
        temp = robot.command('mktemp -d /tmp/zigma-backup.XXXXXX')
        if not __import__('re').fullmatch(r'/tmp/zigma-backup\.[A-Za-z0-9]+', temp):
            raise ValueError('Invalid temporary backup directory')
        remote = temp + '/reader'
        try:
            upload(client, remote, data)
            robot.command('test "$(sha256sum ' + remote + ' | cut -d " " -f 1)" = ' + digest + ' && chmod 700 ' + remote)
            for attempt in range(1, 3):
                print(f'NOR backup attempt {attempt}/2 (32 MiB, followed by a second complete reading).')
                partial.unlink(missing_ok=True)
                try:
                    stdin, stdout, stderr = client.exec_command(remote + ' --nor-stream', timeout=120)
                    stdin.close()
                    try:
                        result = capture(stdout, partial)
                        if stdout.channel.recv_exit_status() != 0:
                            raise ValueError('NOR reader failed; full installation blocked')
                    except Exception as read_error:
                        details = bytearray()
                        while len(details) < 4096 and stdout.channel.recv_stderr_ready():
                            chunk = stdout.channel.recv_stderr(4096 - len(details))
                            if not isinstance(chunk, bytes) or not chunk:
                                break
                            details.extend(chunk)
                        if details:
                            from ssh_output import redact
                            raise RuntimeError(str(read_error) + '; NOR reader: ' +
                                               redact(details.decode('utf-8', 'replace')).strip()) from read_error
                        raise
                    finally:
                        stdout.channel.close()
                    result['robot_host'] = robot.host
                    result['reader_sha256'] = digest
                    manifest.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
                    partial.rename(binary)
                    print('NOR verified and saved: ' + str(binary))
                    print('Manifest: ' + str(manifest) + '; SHA256: ' + result['first_sha256'])
                    return binary
                except (ValueError, OSError) as exc:
                    preserve_unverified(robot, exc)
                    print(str(exc))
                    if attempt == 2 or not isinstance(exc, ValueError) or 'NOR reads differ' not in str(exc):
                        raise RuntimeError('NOR backup not verified; full was NOT installed. ' + str(exc)) from exc
                    print('Restarting BOTH readings from byte zero; no blocks will be reused.')
        finally:
            try:
                robot.command('rm -f ' + remote + '; rmdir ' + temp)
            except Exception as cleanup_error:
                print('[CLEANUP] Backup helper cleanup failed: ' + type(cleanup_error).__name__ + '; original backup error retained.')


def private_json(path, value):
    with path.open('w', encoding='utf-8') as f:
        path.chmod(0o600)
        json.dump(value, f, indent=2)
        f.write('\n')


def preserve_unverified(robot, error):
    """Keep received bytes under an unmistakable name; never publish a good manifest."""
    from ssh_output import redact
    partial = robot.directory/'nor-backup.bin.partial'
    if not partial.exists():
        return None
    stamp = datetime.datetime.now().astimezone().strftime('%Y%m%d-%H%M%S-%f')
    target = robot.directory/('nor-' + stamp + '.unverified.bin')
    partial.rename(target)
    target.chmod(0o600)
    size = target.stat().st_size
    digest = hashlib.sha256()
    with target.open('rb') as stream:
        for block in iter(lambda: stream.read(BLOCK), b''):
            digest.update(block)
    private_json(target.with_suffix('.json'), dict(verified=False, robot_host=robot.host,
                 bytes_received=size, complete_first_read=size == SIZE,
                 file_sha256=digest.hexdigest(), error=redact(str(error)),
                 note='UNVERIFIED: not a validated recovery image; the file hash only identifies received bytes.'))
    print('Unverified received bytes retained: ' + str(target))
    return target


def boot_identity(robot):
    import uuid
    return str(uuid.UUID(robot.command('cat /proc/sys/kernel/random/boot_id', timeout=5).strip()))


def download_with_choice(robot, helper):
    """The owner may explicitly waive the backup, never identity/SSH/firmware checks."""
    from ssh_output import redact
    while True:
        before = None
        try:
            before = boot_identity(robot)
            return download(robot, helper)
        except Exception as exc:
            reason = redact(type(exc).__name__ + ': ' + str(exc))
            preserve_unverified(robot, exc)
            after = None
            try:
                after = boot_identity(robot)
            except Exception:
                pass
            rebooted = before != after if before and after else None
            if rebooted:
                print('[BACKUP] Robot reboot confirmed: its boot ID changed during the failed attempt.')
            elif after is None:
                print('[BACKUP] Robot currently unreachable or unauthenticated; reboot cannot yet be confirmed.')
            else:
                print('[BACKUP] SSH restored in the same boot; no reboot observed during this attempt.')
            stamp = datetime.datetime.now().astimezone().strftime('%Y%m%d-%H%M%S-%f')
            report = robot.directory/('backup-failure-' + stamp + '.json')
            private_json(report, dict(at=datetime.datetime.now().astimezone().isoformat(),
                         robot_host=robot.host, error=reason, boot_before=before, boot_after=after,
                         reboot_detected=rebooted, verified=False))
            print('[BACKUP] ' + reason + '. Report: ' + str(report))
            while True:
                print('NOR backup is NOT verified. Full has not been installed by this attempt.')
                print('R: Retry the complete backup; S: Stop (default); C: Continue without a verified backup.', flush=True)
                choice = input().strip().upper()
                if choice in ('', 'S'):
                    raise RuntimeError('Stopped without installing full. Backup failure: ' + reason) from exc
                if choice == 'R':
                    break
                if choice != 'C':
                    continue
                print('WARNING: continuing modifies the robot without a verified recovery copy. '
                      'A partial/unverified file may be unusable for restoration. Firmware checks still apply.')
                print('Type CONTINUE WITHOUT VERIFIED BACKUP to accept this risk:', flush=True)
                if input().strip() != 'CONTINUE WITHOUT VERIFIED BACKUP':
                    print('Risk not accepted; no installation started.')
                    continue
                try:
                    current = boot_identity(robot)
                except Exception as check_error:
                    print('Cannot continue: authenticated SSH is not ready (' + type(check_error).__name__ +
                          '). Wait for the robot to return, then choose again. Do not resend pairing.')
                    continue
                if before and current != before:
                    print('[BACKUP] Robot reboot confirmed before continuing.')
                private_json(robot.directory/'backup-risk-accepted.json',
                             dict(at=datetime.datetime.now().astimezone().isoformat(), robot_host=robot.host,
                                  verified=False, explicitly_accepted=True, boot_id=current, failure_report=report.name))
                print('Continuing by explicit request WITHOUT a verified NOR backup.')
                return None
