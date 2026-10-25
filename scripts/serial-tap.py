#!/usr/bin/env python3
"""Line-oriented serial reader for the device verify harness (ADR-0009).

Exists as Python rather than Node because the harness measured hand-rolled Node tty
reading to be lossy — 2 lines captured against pyserial's 6 over the same 10 s window —
and a capture that drops lines is a gate that can miss a `Guru Meditation` and report a
false green. pyserial arrives with PlatformIO, which every other gate already requires.

Writes each received line to stdout, flushed, so the Node side sees it immediately.
Exits non-zero on any port error: the harness must never mistake a dead port for a
device that simply said nothing.
"""
import argparse
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("serial-tap: pyserial not importable by this interpreter")

# The ESP32-S3 exposes a native USB CDC port that disappears and re-enumerates across a
# reset, so a reopen window is required rather than a single open attempt.
REENUMERATION_TIMEOUT_S = 10.0
REOPEN_POLL_INTERVAL_S = 0.2

# Classic DTR/RTS reset: RTS drives EN (chip reset), DTR drives IO0 (boot mode). Holding
# IO0 high while pulsing EN low boots the application rather than the ROM loader.
RESET_PULSE_S = 0.1


def open_port(port: str, baud: int, deadline: float) -> "serial.Serial":
    """Open the port, tolerating a device that has not finished enumerating yet."""
    last_error = None
    while time.monotonic() < deadline:
        try:
            return serial.Serial(port, baud, timeout=0.2)
        except (serial.SerialException, OSError) as exc:
            last_error = exc
            time.sleep(REOPEN_POLL_INTERVAL_S)
    sys.exit(f"serial-tap: {port} did not open within "
             f"{REENUMERATION_TIMEOUT_S:.0f}s: {last_error}")


def forward_stdin(handle: "serial.Serial") -> None:
    """Forward lines from stdin to the port, so the orchestrator can drive the firmware.

    Runs on its own thread because the read loop must keep draining the port: a reader that
    pauses to wait for the next command is a reader that drops lines, and a dropped
    `Guru Meditation` is the false green this whole split exists to prevent (ADR-0009).
    The firmware's control channel is line-oriented, so each line is sent with a single
    trailing newline whatever the sender's line endings were (ADR-0014).
    """
    for line in sys.stdin:
        try:
            handle.write(line.rstrip("\r\n").encode("utf-8", "replace") + b"\n")
            handle.flush()
        except (serial.SerialException, OSError):
            return


def pulse_reset(handle: "serial.Serial") -> None:
    """Reset the board so the boot banner is emitted after the reader is already listening."""
    handle.dtr = False
    handle.rts = True
    time.sleep(RESET_PULSE_S)
    handle.rts = False
    time.sleep(RESET_PULSE_S)


def main() -> int:
    parser = argparse.ArgumentParser(description="Stream serial lines to stdout.")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, required=True)
    parser.add_argument("--seconds", type=float, required=True,
                        help="How long to keep reading before exiting cleanly.")
    parser.add_argument("--reset", action="store_true",
                        help="Pulse DTR/RTS once the port is open, to capture a boot.")
    parser.add_argument("--send-stdin", action="store_true",
                        help="Forward lines from stdin to the port, to drive the firmware.")
    args = parser.parse_args()

    handle = open_port(args.port, args.baud,
                       time.monotonic() + REENUMERATION_TIMEOUT_S)
    if args.reset:
        pulse_reset(handle)

    if args.send_stdin:
        threading.Thread(target=forward_stdin, args=(handle,), daemon=True).start()

    # Signal readiness only after any reset has been asserted, so the caller knows that
    # everything printed from here on belongs to the run it asked for.
    print("<tap-ready>", flush=True)

    # Bytes are accumulated and split on newlines here rather than using readline(),
    # which returns whatever it has when its timeout expires and therefore emits partial
    # lines. That matters: a fragmented line could split "Guru Meditation" across two
    # emitted lines and walk straight past the harness's failure patterns.
    deadline = time.monotonic() + args.seconds
    pending = b""
    try:
        while time.monotonic() < deadline:
            waiting = handle.in_waiting
            chunk = handle.read(waiting if waiting else 1)
            if not chunk:
                continue
            pending += chunk
            *complete, pending = pending.split(b"\n")
            for line in complete:
                sys.stdout.write(line.decode("utf-8", "replace").rstrip("\r") + "\n")
            sys.stdout.flush()
        if pending:
            # A trailing fragment with no newline is still evidence; label it so a reader
            # can tell it apart from a line the device actually terminated.
            sys.stdout.write(pending.decode("utf-8", "replace").rstrip("\r")
                             + "  <tap-unterminated>\n")
            sys.stdout.flush()
    except BrokenPipeError:
        # The consumer stopped reading; that is its decision, not a device failure.
        return 0
    except (serial.SerialException, OSError) as exc:
        sys.exit(f"serial-tap: read failed on {args.port}: {exc}")
    finally:
        handle.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
