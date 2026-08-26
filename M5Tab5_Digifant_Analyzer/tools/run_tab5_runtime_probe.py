#!/usr/bin/env python3
"""Bounded host driver for the TAB5_RUNTIME_DEBUG firmware build.

Start it immediately after reset (or immediately after upload) so the trace also
contains the last healthy DEBUG_STATUS before a stall.  It depends only on the
Python standard library and never sends logger commands.
"""

import argparse
import datetime as dt
import os
import select
import sys
import termios
import time


def configure_raw(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attributes = termios.tcgetattr(fd)
    attributes[0] = termios.IGNPAR
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def emit(log, text: str) -> None:
    stamp = dt.datetime.now().isoformat(timespec="milliseconds")
    line = f"{stamp} {text}"
    print(line, flush=True)
    log.write(line + "\n")
    log.flush()


def command_for_mode(mode: str, sequence: int) -> str | None:
    if mode == "a":
        return f"TAB {sequence % 4}"
    if mode == "d":
        return f"TAB {sequence % 3}"
    if mode == "e":
        return "TAB 3" if sequence == 0 else None
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=("a", "b", "c", "d", "e"), required=True)
    parser.add_argument("--minutes", type=float, default=30.0)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    fd = configure_raw(args.port)
    deadline = time.monotonic() + args.minutes * 60.0
    next_tab = time.monotonic()
    next_debug = time.monotonic() + 2.0
    pending_debug_deadline: float | None = None
    tab_sequence = 0
    line_buffer = bytearray()
    debug_replies = 0
    rx_total = 0
    last_tx = ""
    try:
        with open(args.log, "w", encoding="utf-8") as log:
            emit(log, f"PROBE_START mode={args.mode} port={args.port} duration_s={args.minutes * 60:.0f}")
            while time.monotonic() < deadline:
                now = time.monotonic()
                if args.mode in ("a", "d", "e") and now >= next_tab:
                    command = command_for_mode(args.mode, tab_sequence)
                    if command is not None:
                        os.write(fd, (command + "\n").encode("ascii"))
                        last_tx = command
                        emit(log, f"TX {command}")
                    tab_sequence += 1
                    next_tab += 2.0 if args.mode in ("a", "d") else args.minutes * 60.0
                if now >= next_debug:
                    os.write(fd, b"DEBUG_STATUS\n")
                    last_tx = "DEBUG_STATUS"
                    emit(log, "TX DEBUG_STATUS")
                    next_debug += 10.0
                    pending_debug_deadline = now + 4.0

                readable, _, _ = select.select((fd,), (), (), 0.2)
                if readable:
                    data = os.read(fd, 4096)
                    rx_total += len(data)
                    for value in data:
                        if value == 10:
                            text = line_buffer.decode("utf-8", errors="replace").rstrip("\r")
                            line_buffer.clear()
                            if text:
                                emit(log, f"RX {text}")
                                if text.startswith("DEBUG_SERIAL "):
                                    debug_replies += 1
                                    pending_debug_deadline = None
                        elif len(line_buffer) < 4096:
                            line_buffer.append(value)
                if pending_debug_deadline is not None and now >= pending_debug_deadline:
                    if line_buffer:
                        partial = line_buffer.decode("utf-8", errors="replace")
                        emit(log, f"RX_PARTIAL bytes={len(line_buffer)} text={partial!r}")
                    emit(
                        log,
                        "PROBE_STATE "
                        f"rx_total={rx_total} last_tx={last_tx!r} "
                        f"debug_replies={debug_replies} "
                        f"debug_pending={pending_debug_deadline is not None} "
                        f"partial_bytes={len(line_buffer)}",
                    )
                    emit(log, "PROBE_FAILURE missing DEBUG_STATUS reply for 4 s")
                    return 2
            emit(log, f"PROBE_PASS debug_replies={debug_replies}")
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
