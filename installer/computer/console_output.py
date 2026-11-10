"""Timestamp CLI output line by line, preserving normal terminal behavior."""
import datetime
import sys
import threading


class TimestampedStream:
    def __init__(self, stream):
        self.stream = stream
        self.line_start = True
        self.lock = threading.RLock()

    def write(self, text):
        with self.lock:
            for part in text.splitlines(keepends=True):
                if self.line_start:
                    stamp = datetime.datetime.now().astimezone().isoformat(timespec='milliseconds')
                    self.stream.write(f'[{stamp}] ')
                self.stream.write(part)
                self.line_start = part.endswith('\n') or part.endswith('\r')
            self.stream.flush()
        return len(text)

    def flush(self):
        with self.lock:
            self.stream.flush()

    def __getattr__(self, name):
        return getattr(self.stream, name)


def configure_output():
    for name in ('stdout', 'stderr'):
        stream = getattr(sys, name)
        if not isinstance(stream, TimestampedStream):
            setattr(sys, name, TimestampedStream(stream))
