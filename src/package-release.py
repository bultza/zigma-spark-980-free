#!/usr/bin/env python3
"""Assemble deployment archives from this checkout's compiled components."""

import argparse
import hashlib
import re
import runpy
import shutil
from pathlib import Path
from tempfile import TemporaryDirectory


REPO = Path(__file__).resolve().parents[1]
ARM_OUTPUTS = (
    'free-zigma-agent-arm', 'zigma-observe-arm', 'zigma-core-arm',
    'zigma-network-arm', 'zigma-area-arm', 'zigma-zones-arm',
    'zigma-maps-arm', 'zigma-options-arm', 'zigmactl',
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_package(root):
    def copy(source, destination):
        if not source.is_file() or source.is_symlink():
            raise ValueError(f'Missing regular package input: {source}')
        target = root / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    installer_files = {
        'computer': (
            'console_output.py', 'hosts_guard.py', 'installer.py', 'known-firmware.json',
            'nor_backup.py', 'requirements-wizard.txt', 'run-windows.cmd',
            'send_pairing.py', 'session_recovery.py', 'ssh_output.py',
            'start-linux.sh', 'start-windows.cmd', 'wizard.py',
        ),
        'robot': (
            'activate.sh', 'configure-support.sh', 'install.sh.in', 'storage.sh',
            'vendor-hosts.sh', 'init/S01time', 'init/S03zigma-hostname',
            'init/S96ntpsync', 'init/S97crond', 'init/S98zigma-free',
            'src/zigma-cron', 'src/zigma-free-service', 'src/zigma-mode',
            'src/zigma-mode-backend', 'src/zigma-settings', 'src/zigma-time-sync',
        ),
    }
    for folder, names in installer_files.items():
        for name in names:
            relative = Path(folder) / name
            copy(REPO / 'installer' / relative, relative)

    robot = REPO / 'src/robot'
    build = robot / 'build/musl'
    transport = build / 'transport'
    for name in ARM_OUTPUTS:
        copy(build / name, Path('robot/build') / name)
    for name in ('zigma-curl-arm', 'telegram-ca.pem'):
        copy(transport / name, Path('robot/build') / name)
    for name in (*ARM_OUTPUTS, 'zigma-curl-arm'):
        binary = root / 'robot/build' / name
        if b'GLIBC_' in binary.read_bytes():
            raise ValueError(f'Unexpected glibc-linked package input: {binary.name}')

    source_hash, = re.findall(rb'^## SHA256: ([0-9a-f]{64})$',
                             (transport / 'telegram-ca.pem').read_bytes(), re.MULTILINE)
    if sha256(transport / 'certdata.txt') != source_hash.decode():
        raise ValueError('The Mozilla source does not match the packaged CA bundle')
    copy(transport / 'certdata.txt', 'third-party/certdata.txt')
    copy(transport / 'musl-1.2.6.tar.gz', 'third-party/musl-1.2.6.tar.gz')
    for name in ('README.md', 'musl-COPYRIGHT', 'GPL-3.0', 'GCC-RUNTIME-EXCEPTION', 'MPL-2.0'):
        copy(robot / 'vendor/notices' / name, Path('third-party') / name)

    notices = root / 'robot/build/telegram-licenses.txt'
    notices.write_text(
        'curl\n\n' + (transport / 'curl-LICENSE').read_text()
        + '\nMbed TLS\n\n' + (transport / 'mbedtls-LICENSE').read_text()
    )
    for name in ('cjson', 'mbedtls'):
        relative = Path('vendor') / name / 'LICENSE'
        copy(robot / relative, Path('robot') / relative)
    copy(robot / 'versions.json', 'robot/versions.json')
    for name in ('app.js', 'favicon.svg', 'index.html', 'nor.js', 'style.css'):
        copy(REPO / 'src/web' / name, Path('web/src') / name)

    copy(REPO / 'LICENSE', 'LICENSE')
    build_guide = (REPO / 'installer/BUILD.md').read_text()
    build_guide = build_guide.replace(
        '(../src/README.md)',
        '(https://github.com/bultza/zigma-spark-980-free/blob/main/src/README.md)',
    )
    (root / 'BUILD.md').write_text(build_guide)
    copy(REPO / 'installer/README.md', 'README.md')
    (root / 'start-windows.cmd').write_bytes(
        b'@echo off\r\ncall "%~dp0computer\\start-windows.cmd" %*\r\n'
    )
    linux_launcher = root / 'start-linux.sh'
    linux_launcher.write_text(
        '#!/bin/sh\nexec sh "$(dirname "$0")/computer/start-linux.sh" "$@"\n'
    )
    linux_launcher.chmod(0o755)

    # Validate the same files and versions the wizard uses to generate its payload.
    installer = runpy.run_path(str(root / 'computer/installer.py'))
    installer['build']('full', 'ecdsa-sha2-nistp256 fixture', 'http://192.0.2.1:8000',
                       'Vacuum', telegram=True)
    files = sorted(path for path in root.rglob('*') if path.is_file())
    (root / 'SHA256SUMS').write_text(''.join(
        f'{sha256(path)}  {path.relative_to(root).as_posix()}\n' for path in files
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', default='dev', help='Version or development label for the archive')
    parser.add_argument('--output-dir', type=Path, default=REPO / 'dist')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', args.label):
        parser.error('Invalid archive label')
    if args.label.lower() in {'1.0', 'v1.0'}:
        parser.error('V1.0 identifies the accepted historical package. Choose a new label.')

    name = f'zigma-deployment-{args.label}'
    output = args.output_dir.resolve()
    formats = {'zip': '.zip', 'gztar': '.tar.gz'}
    for suffix in formats.values():
        for ending in (suffix, suffix + '.sha256'):
            target = output / (name + ending)
            if target.exists() or target.is_symlink():
                parser.error(f'Output already exists: {target}')

    try:
        with TemporaryDirectory() as directory:
            stage = Path(directory)
            stage_package(stage / name)
            archives = [Path(shutil.make_archive(str(stage / name), kind, stage, name))
                        for kind in formats]
            output.mkdir(parents=True, exist_ok=True)
            for archive in archives:
                with (output / archive.name).open('xb') as destination, archive.open('rb') as source:
                    shutil.copyfileobj(source, destination)
                checksum = output / (archive.name + '.sha256')
                with checksum.open('x') as destination:
                    destination.write(f'{sha256(archive)}  {archive.name}\n')
                print(output / archive.name)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Packaging failed: {error}\n')


if __name__ == '__main__':
    main()
