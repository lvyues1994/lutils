"""Run one native test in an isolated ADB directory and preserve its exit status."""

import argparse
import os
import pathlib
import shlex
import signal
import socket
import re
import subprocess
import sys
import uuid


def verify_server(adb, address, serial):
    """Check the explicit endpoint without asking adb to create or replace a server."""
    if not address.startswith("tcp:"):
        raise ValueError("explicit server socket must be tcp:host:port")
    host, separator, port = address[4:].rpartition(":")
    if not separator or not host or not port.isdigit() or not 1 <= int(port) <= 65535:
        raise ValueError("invalid ADB server socket")
    def query(request):
        with socket.create_connection((host, int(port)), timeout=5) as connection:
            connection.sendall(f"{len(request):04x}".encode() + request.encode())
            def receive(size):
                data = b""
                while len(data) < size:
                    chunk = connection.recv(size - len(data))
                    if not chunk:
                        raise ValueError("ADB server closed its response")
                    data += chunk
                return data
            if receive(4) != b"OKAY":
                raise ValueError("ADB server rejected the read-only preflight")
            return receive(int(receive(4), 16)).decode()
    version = subprocess.run([adb, "version"], capture_output=True, text=True,
                             check=True, timeout=5).stdout
    client = re.search(r"Android Debug Bridge version 1\.0\.(\d+)", version)
    if not client or int(client[1]) != int(query("host:version"), 16):
        raise ValueError("ADB client/server version mismatch; leave the shared server unchanged")
    devices = [line.split()[:2] for line in query("host:devices-l").splitlines()]
    if [serial, "device"] not in devices:
        raise ValueError("the selected Android device is not ready on this server")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--server-socket", help="route all calls to this existing ADB server")
    parser.add_argument("--timeout", type=int, default=30, help="remote deadline in seconds (requires device timeout)")
    parser.add_argument("--push-timeout", type=int, default=60,
                        help="ADB push deadline in seconds (default: 60)")
    parser.add_argument("--no-compression", action="store_true",
                        default=os.environ.get("LUTILS_ANDROID_NO_COMPRESSION") == "1",
                        help="disable push compression (also LUTILS_ANDROID_NO_COMPRESSION=1)")
    parser.add_argument("--cpu", type=int, help="pin only the test process to this CPU")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 600:
        parser.error("--timeout must be in 1..600 seconds")
    if args.cpu is not None and not 0 <= args.cpu <= 255:
        parser.error("--cpu must be in 0..255")
    if not 1 <= args.push_timeout <= 600:
        parser.error("--push-timeout must be in 1..600")
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command or not pathlib.Path(command[0]).is_file():
        parser.error("a local executable is required")
    server_socket = args.server_socket or os.environ.get("ADB_SERVER_SOCKET")
    if server_socket:
        verify_server(args.adb, server_socket, args.serial)
    adb = [args.adb, "-s", args.serial]
    remote = "/data/local/tmp/lutils-" + uuid.uuid4().hex
    environment = dict(os.environ)
    if server_socket:
        environment["ADB_SERVER_SOCKET"] = server_socket

    def shell(words, **kwargs):
        return subprocess.run(adb + ["shell", shlex.join(words)], timeout=15, env=environment, **kwargs)

    result = 2
    try:
        shell(["mkdir", remote], check=True)
        push = adb + ["push"] + (["-Z"] if args.no_compression else [])
        subprocess.run(push + [command[0], remote + "/test"], check=True,
                       stdout=subprocess.DEVNULL, timeout=args.push_timeout, env=environment)
        shell(["chmod", "700", remote + "/test"], check=True)
        # Tests may emit relative output files. Keep those in the owned directory.
        invocation = ["./test"] + command[1:]
        if args.cpu is not None:
            invocation = ["taskset", format(1 << args.cpu, "x")] + invocation
        if args.timeout is not None:
            invocation = ["timeout", "-k", "5", str(args.timeout)] + invocation
        script = "cd " + shlex.quote(remote) + " && " + shlex.join(invocation)
        result = subprocess.run(adb + ["shell", script],
                                timeout=args.timeout + 10, env=environment).returncode
    finally:
        try:
            cleaned = shell(["rm", "-rf", remote], check=False).returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            cleaned = False
        if not cleaned:
            print(f"Could not clean isolated device directory: {remote}", file=sys.stderr)
            if result == 0:
                result = 2
    return result


if __name__ == "__main__":
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    sys.exit(main())
