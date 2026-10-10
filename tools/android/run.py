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
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command or not pathlib.Path(command[0]).is_file():
        parser.error("a local executable is required")
    adb = [args.adb, "-s", args.serial]
    remote = "/data/local/tmp/lutils-" + uuid.uuid4().hex

    def shell(words, **kwargs):
        return subprocess.run(adb + ["shell", shlex.join(words)], **kwargs)

    shell(["mkdir", remote], check=True)
    try:
        subprocess.run(adb + ["push", command[0], remote + "/test"], check=True,
                       stdout=subprocess.DEVNULL)
        shell(["chmod", "700", remote + "/test"], check=True)
        # Tests may emit relative output files. Keep those in the owned directory.
        script = "cd " + shlex.quote(remote) + " && " + shlex.join(["./test"] + command[1:])
        return subprocess.run(adb + ["shell", script]).returncode
    finally:
        shell(["rm", "-rf", remote], check=False)


if __name__ == "__main__":
    sys.exit(main())
