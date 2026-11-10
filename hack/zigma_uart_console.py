#!/usr/bin/env python3
"""Interactive Zigma ttyS2 console with a one-shot UART unlock request.

Run on a Linux/macOS computer connected to the robot's UART. The first Enter
sends the unlock frame; subsequent input is passed through unchanged.
"""

import argparse
import os
import select
import sys
import termios
import tty


UNLOCK_FRAME = bytes.fromhex("05 02 00 08 22 BC 57 C1 3C C7 CC A4 91 9A 0A")
EXIT_BYTE = 0x1D  # Ctrl-]


def configure_serial(fd: int) -> None:
    tty.setraw(fd, termios.TCSANOW)
    settings = termios.tcgetattr(fd)
    settings[2] &= ~(
        termios.CSIZE
        | termios.PARENB
        | termios.CSTOPB
        | getattr(termios, "CRTSCTS", 0)
    )
    settings[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
    settings[4] = termios.B115200
    settings[5] = termios.B115200
    settings[6][termios.VMIN] = 1
    settings[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, settings)


def console(serial_fd: int) -> None:
    input_fd = sys.stdin.fileno()
    output_fd = sys.stdout.fileno()
    pending = bytearray()
    unlocked = False

    print(
        "[Zigma UART: 115200 8N1. Wait for Linux/debug_server, then press Enter "
        "to send unlock once. Ctrl-] exits.]",
        file=sys.stderr,
        flush=True,
    )

    while True:
        readable, writable, _ = select.select(
            [serial_fd, input_fd], [serial_fd] if pending else [], []
        )

        if serial_fd in readable:
            try:
                incoming = os.read(serial_fd, 4096)
            except BlockingIOError:
                incoming = None
            if incoming == b"":
                print("\r\n[UART disconnected]", file=sys.stderr)
                return
            if incoming:
                view = memoryview(incoming)
                while view:
                    view = view[os.write(output_fd, view) :]

        if input_fd in readable:
            typed = os.read(input_fd, 1024)
            if not typed:
                return
            for key in typed:
                if key == EXIT_BYTE:
                    return
                if not unlocked and key in (0x0A, 0x0D):
                    pending.extend(UNLOCK_FRAME)
                    unlocked = True
                    print("\r\n[Unlock frame queued; UART now passes keys through.]", file=sys.stderr, flush=True)
                else:
                    pending.append(key)

        if serial_fd in writable and pending:
            try:
                sent = os.write(serial_fd, pending)
            except BlockingIOError:
                continue
            del pending[:sent]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="Host serial device, for example /dev/ttyUSB0")
    args = parser.parse_args()

    if not sys.stdin.isatty() or not sys.stdout.isatty():
        parser.error("stdin and stdout must be interactive terminals")

    input_fd = sys.stdin.fileno()
    input_settings = termios.tcgetattr(input_fd)
    serial_fd = None
    serial_settings = None
    try:
        serial_fd = os.open(args.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        serial_settings = termios.tcgetattr(serial_fd)
        configure_serial(serial_fd)
        tty.setraw(input_fd, termios.TCSANOW)
        console(serial_fd)
    except OSError as error:
        print(f"\r\n[UART error: {error}]", file=sys.stderr)
        return 1
    finally:
        termios.tcsetattr(input_fd, termios.TCSANOW, input_settings)
        if serial_fd is not None:
            if serial_settings is not None:
                try:
                    termios.tcsetattr(serial_fd, termios.TCSANOW, serial_settings)
                except OSError:
                    pass  # The adapter may have been unplugged.
            os.close(serial_fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
