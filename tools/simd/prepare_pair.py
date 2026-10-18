"""Build two ancestral commits with the candidate's common benchmark harness."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tarfile

sys.dont_write_bytecode = True


def run_bounded(command, log, timeout):
    # Each build/test owns its process group. Reap children before scratch cleanup.
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = process.wait(timeout=timeout)
        if code:
            raise subprocess.CalledProcessError(code, command)
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        # The parent may have exited before an ignoring grandchild. Kill the
        # owned group even when wait() has already reaped that parent.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True,
                                   stderr=subprocess.PIPE, timeout=30).strip()


def revisions(repo, before, after):
    def resolve(ref):
        value = git(repo, "rev-parse", "--verify", "--end-of-options", ref + "^{commit}")
        if not re.fullmatch(r"[0-9a-f]{40}", value) or set(value) == {"0"}:
            raise ValueError("expected a nonzero commit SHA")
        return value
    old, new = resolve(before), resolve(after)
    if old == new:
        raise ValueError("before and after must be different commits")
    subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", old, new],
                   check=True, timeout=30)
    return {"before": old, "after": new}


def prepare(repo, before, after, output):
    if sys.version_info < (3, 12):
        raise ValueError("Python 3.12+ is required for safe archive extraction")
    refs = revisions(repo, before, after)
    output.mkdir(parents=True, exist_ok=False)
    for role, revision in refs.items():
        source = output / (role + "-source")
        source.mkdir()
        archive = output / (role + ".tar")
        subprocess.run(["git", "-C", str(repo), "archive", "--format=tar", "-o", str(archive), revision],
                       check=True, timeout=60)
        with tarfile.open(archive) as tar:
            tar.extractall(source, filter="data")
        archive.unlink()
    common = output / "after-source/benchmarks/simd"
    destination = output / "before-source/benchmarks/simd"
    shutil.rmtree(destination)
    shutil.copytree(common, destination)
    for role in refs:
        source, build = output / (role + "-source"), output / role
        with (output / (role + "-build.log")).open("w") as log:
            commands = [
                ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_STANDARD=17",
                 "-DCMAKE_CXX_EXTENSIONS=OFF", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                 "-DLUTILS_BUILD_BENCHMARKS=ON", "-DLUTILS_BUILD_TESTS=ON",
                 "-DLUTILS_BUILD_EXAMPLES=OFF", "-DLUTILS_INSTALL=OFF"],
                ["cmake", "--build", str(build), "-j2"],
                ["ctest", "--test-dir", str(build), "-R", "^simd\\.", "--output-on-failure",
                 "--no-tests=error"]]
            for command, timeout in zip(commands, (60, 300, 60)):
                run_bounded(command, log, timeout)
    (output / "revisions.json").write_text(json.dumps(refs, indent=2) + "\n")
    return refs


def main():
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--before", required=True)
    parser.add_argument("--after", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.repo.resolve(), args.before, args.after, args.output.resolve())))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired,
            tarfile.TarError) as error:
        print(f"pair preparation failed (no fallback harness): {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
