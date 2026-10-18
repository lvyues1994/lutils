"""Run one native test in an isolated ADB directory and preserve its exit status."""

import argparse
import pathlib
import shlex
import subprocess
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--timeout", type=int, help="remote deadline in seconds (requires device timeout)")
    parser.add_argument("--push-timeout", type=int, default=60,
                        help="ADB push deadline in seconds (default: 60)")
    parser.add_argument("--cpu", type=int, help="pin only the test process to this CPU")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.timeout is not None and args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.cpu is not None and not 0 <= args.cpu <= 255:
        parser.error("--cpu must be in 0..255")
    if not 1 <= args.push_timeout <= 600:
        parser.error("--push-timeout must be in 1..600")
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command or not pathlib.Path(command[0]).is_file():
        parser.error("a local executable is required")
    adb = [args.adb, "-s", args.serial]
    remote = "/data/local/tmp/lutils-" + uuid.uuid4().hex

    def shell(words, **kwargs):
        return subprocess.run(adb + ["shell", shlex.join(words)], timeout=15, **kwargs)

    shell(["mkdir", remote], check=True)
    result = 2
    try:
        subprocess.run(adb + ["push", command[0], remote + "/test"], check=True,
                       stdout=subprocess.DEVNULL, timeout=args.push_timeout)
        shell(["chmod", "700", remote + "/test"], check=True)
        # Tests may emit relative output files. Keep those in the owned directory.
        invocation = ["./test"] + command[1:]
        if args.cpu is not None:
            invocation = ["taskset", format(1 << args.cpu, "x")] + invocation
        if args.timeout is not None:
            invocation = ["timeout", "-k", "5", str(args.timeout)] + invocation
        script = "cd " + shlex.quote(remote) + " && " + shlex.join(invocation)
        result = subprocess.run(adb + ["shell", script],
                                timeout=args.timeout + 10 if args.timeout else None).returncode
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
    sys.exit(main())
