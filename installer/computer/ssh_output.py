"""Bounded concurrent SSH output collection and private, redacted diagnostics."""
import datetime
import json
import re
import time

LIMIT = 1024 * 1024


def redact(text):
    text = re.sub(r'-----BEGIN [^-]*PRIVATE KEY-----.*?(?:-----END [^-]*PRIVATE KEY-----|\Z)', '[PRIVATE KEY REDACTED]', text, flags=re.S)
    text = re.sub(r'(?i)(Bearer\s+)\S+', r'\1[REDACTED]', text)
    text = re.sub(r'''(?im)((?:password|passwd|psk|token|secret|private_key)\s*[=:]\s*)(?:"[^"\n]*"|'[^'\n]*'|[^\s,;]+)''', r'\1[REDACTED]', text)
    return text


def collect(channel, timeout):
    chunks = [bytearray(), bytearray()]
    counts = [0, 0]
    deadline = time.monotonic() + timeout
    timed_out = False
    while True:
        for index, ready, receive in ((0, channel.recv_ready, channel.recv), (1, channel.recv_stderr_ready, channel.recv_stderr)):
            if ready():
                data = receive(32768)
                counts[index] += len(data)
                chunks[index].extend(data[:max(0, LIMIT - len(chunks[index]))])
        if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
            break
        if time.monotonic() >= deadline:
            timed_out = True
            channel.close()
            break
        time.sleep(.01)
    code = None if timed_out else channel.recv_exit_status()
    return bytes(chunks[0]), bytes(chunks[1]), code, timed_out, counts


def report(directory, command, out, error, code, timed_out, counts, phase=None):
    now = datetime.datetime.now().astimezone()
    record = dict(at=now.isoformat(), phase=phase or 'ssh-command', command=redact(command),
                  exit_code=code, timed_out=timed_out, received_bytes=counts,
                  truncated=[n > LIMIT for n in counts],
                  stdout=redact(out.decode('utf-8', 'replace')) if phase else '[withheld: command may return credentials]',
                  stderr=redact(error.decode('utf-8', 'replace')))
    name = directory/('diagnostic-' + now.strftime('%Y%m%d-%H%M%S-%f') + '.json')
    with name.open('x', encoding='utf-8') as f:
        name.chmod(0o600)
        json.dump(record, f, indent=2)
        f.write('\n')
    return name, record


def report_exception(exc, directory=None):
    """Always show the exception type; retain stack locations without secrets/locals."""
    from pathlib import Path
    import sys
    import traceback
    kind = type(exc).__name__
    detail = redact(str(exc)).strip()
    if not detail:
        detail = ('SSH connection closed unexpectedly before completing the operation.'
                  if isinstance(exc, EOFError) else 'No additional error message was provided.')
    message = kind + ': ' + detail
    print('ERROR: ' + message, file=sys.stderr, flush=True)
    try:
        directory = Path(directory) if directory is not None else Path.home()/'Zigma'/'diagnostics'
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        now = datetime.datetime.now().astimezone()
        name = directory/('error-'+now.strftime('%Y%m%d-%H%M%S-%f')+'.json')
        frames = [{'file': frame.filename, 'line': frame.lineno, 'function': frame.name}
                  for frame in traceback.extract_tb(exc.__traceback__)]
        record = {'at': now.isoformat(), 'type': kind, 'message': detail, 'stack': frames}
        with name.open('x', encoding='utf-8') as output:
            name.chmod(0o600)
            json.dump(record, output, indent=2)
            output.write('\n')
        print('[DIAGNOSTIC] Saved: '+str(name), file=sys.stderr, flush=True)
        return name
    except OSError as save_error:
        print('[DIAGNOSTIC] Cannot save error report: '+type(save_error).__name__, file=sys.stderr, flush=True)
        return None
