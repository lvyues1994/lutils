"""Capture independent processes in ABBA/BAAB blocks and compare within each block."""
import argparse
import json
import math
from pathlib import Path
import statistics as stats
import subprocess
import sys
import time

sys.dont_write_bytecode = True
import regression as r


def schedule(blocks):
    if type(blocks) is not int or not 5 <= blocks <= 10:
        raise ValueError("use 5..10 complete blocks")
    return [(block, position, role) for block in range(blocks)
            for position, role in enumerate(
                ("before", "after", "after", "before") if block % 2 == 0 else
                ("after", "before", "before", "after"))]


def compatibility(before, after):
    """Freeze measurement semantics; retain implementation changes as evidence."""
    def measurement(build):
        return {"settings": build["settings"], "harness": build["harness"],
                "compilers": sorted({json.dumps(v["compiler"], sort_keys=True)
                                     for v in build["units"].values()}),
                "harness_units": {k: v for k, v in build["units"].items()
                                  if k.startswith("benchmarks/simd/")}}
    a, b = measurement(before), measurement(after)
    for key in a:
        if a[key] != b[key]:
            raise ValueError(f"incompatible measurement: {key}")
    old, new = before["units"], after["units"]
    return {"added_units": sorted(new.keys() - old.keys()),
            "removed_units": sorted(old.keys() - new.keys()),
            "changed_units": {k: {"before": old[k], "after": new[k]}
                              for k in sorted(old.keys() & new.keys()) if old[k] != new[k]},
            "link_changes": {k: {"before": before["links"].get(k), "after": after["links"].get(k)}
                             for k in sorted(before["links"].keys() | after["links"].keys())
                             if before["links"].get(k) != after["links"].get(k)}}


def capture(args):
    order = schedule(args.blocks)
    if not 0 <= args.cpu <= 255:
        raise ValueError("CPU must be in 0..255")
    builds = {"before": args.before_build.resolve(), "after": args.after_build.resolve()}
    binary_name, options = r.SUITES[args.suite]
    binaries = {role: build / "benchmarks/simd" / binary_name for role, build in builds.items()}
    versions = {role: {"revision": getattr(args, role + "_revision"),
                       "build": r.build_info(build), "binary_sha256": r.digest(binaries[role])}
                for role, build in builds.items()}
    changes = compatibility(versions["before"]["build"], versions["after"]["build"])
    machine = r.machine_info(args.cpu, args.serial)
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": 1, "protocol": "abba-v1", "complete": False,
                "machine_id": args.machine_id, "machine": machine, "versions": versions,
                "suite": args.suite, "blocks": args.blocks, "implementation_changes": changes,
                "runs": []}
    path = args.output / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    environment, keys = None, None
    for block, position, role in order:
        stem = f"block-{block:02}-{position}-{role}"
        output = args.output / (stem + ".jsonl")
        invocation = [str(binaries[role]), *options]
        if args.serial:
            invocation = [sys.executable, str(r.ROOT / "tools/android/run.py"),
                          "--serial", args.serial, "--cpu", str(args.cpu), "--timeout", "50",
                          "--push-timeout", str(args.push_timeout), "--", *invocation]
        else:
            invocation = ["taskset", "-c", str(args.cpu), *invocation]
        started = time.monotonic_ns()
        with output.open("w") as out, (args.output / (stem + ".stderr")).open("w") as err:
            subprocess.run(invocation, check=True, stdout=out, stderr=err,
                           timeout=135 + args.push_timeout if args.serial else 50)
        ended = time.monotonic_ns()
        env, cases = r.load_run(output, args.cpu)
        if environment is not None and (environment != env or keys != set(cases)):
            raise ValueError("run environment/case inventory changed during session")
        environment, keys = env, set(cases)
        manifest["runs"].append({"block": block, "position": position, "role": role,
                                 "file": output.name, "sha256": r.digest(output),
                                 "started_ns": started, "ended_ns": ended})
        path.write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"{args.suite}: block {block + 1}/{args.blocks} {position + 1}/4 {role} "
              f"({len(cases)} validated cases)", flush=True)
    if r.machine_info(args.cpu, args.serial) != machine:
        raise ValueError("machine configuration changed during session")
    for role, build in builds.items():
        if (r.digest(binaries[role]) != versions[role]["binary_sha256"]
                or r.build_info(build) != versions[role]["build"]):
            raise ValueError(f"{role}: binary or build changed during session")
    manifest.update(complete=True, environment=environment)
    path.write_text(json.dumps(manifest, indent=2) + "\n")


def load(path):
    manifest = json.loads((path / "manifest.json").read_text())
    if (not isinstance(manifest, dict) or manifest.get("schema") != 1 or manifest.get("protocol") != "abba-v1"
            or manifest.get("complete") is not True):
        raise ValueError("incomplete or unsupported paired session")
    order = schedule(manifest["blocks"])
    records = manifest["runs"]
    if not isinstance(records, list) or len(records) != len(order):
        raise ValueError("incomplete ABBA blocks")
    cpu = manifest["machine"]["affinity"]
    if (not isinstance(cpu, list) or len(cpu) != 1 or type(cpu[0]) is not int
            or not 0 <= cpu[0] <= 255):
        raise ValueError("session requires one valid CPU")
    changes = compatibility(manifest["versions"]["before"]["build"],
                            manifest["versions"]["after"]["build"])
    if changes != manifest["implementation_changes"]:
        raise ValueError("inconsistent implementation differences")
    runs, names, hashes, previous_end = [], set(), set(), 0
    for expected, record in zip(order, records):
        if tuple(record[k] for k in ("block", "position", "role")) != expected:
            raise ValueError("invalid ABBA/BAAB process order")
        start, end = record["started_ns"], record["ended_ns"]
        if type(start) is not int or type(end) is not int or not previous_end <= start < end:
            raise ValueError("overlapping or invalid process times")
        previous_end = end
        name = record["file"]
        if Path(name).name != name or name in names:
            raise ValueError("invalid or duplicated run path")
        file = path / name
        if file.is_symlink() or not file.is_file() or file.resolve().parent != path.resolve():
            raise ValueError("run must be a regular file inside the session")
        digest = r.digest(file)
        if digest != record["sha256"] or digest in hashes:
            raise ValueError("corrupt or duplicated run")
        env, cases = r.load_run(file, cpu[0])
        if env != manifest["environment"] or (runs and set(cases) != set(runs[0])):
            raise ValueError("run environment/cases differ")
        names.add(name)
        hashes.add(digest)
        runs.append(cases)
    return manifest, runs


def compare(path, threshold=0.05, noise=0.03):
    if not 0 < threshold < 1 or not 0 < noise < 1:
        raise ValueError("threshold and noise must be between zero and one")
    manifest, runs = load(path)
    rows = []
    for key in sorted(runs[0]):
        groups = {role: [] for role in ("before", "after")}
        ratios = []
        for block in range(manifest["blocks"]):
            log_ratio = 0.0
            for position in range(4):
                index = 4 * block + position
                role = manifest["runs"][index]["role"]
                case = runs[index][key]
                groups[role].append(case)
                log_ratio += (1 if role == "after" else -1) * math.log(case["median_ns"]) / 2
            ratios.append(math.exp(log_ratio))
        center = stats.median(ratios)
        mad = stats.median(abs(x - center) for x in ratios) / center
        spread = (sorted(ratios)[-2] - sorted(ratios)[1]) / center
        old, new = (r.summary(groups[role]) for role in ("before", "after"))
        noisy = mad > noise or spread > 4 * noise or max(
            old["within_mad_ratio"], new["within_mad_ratio"]) > noise
        status = "inconclusive" if noisy else "pass"
        if not noisy and center > 1 + threshold:
            losses = sum(v > 1 + threshold for v in ratios)
            status = ("regression" if losses >= math.ceil(0.8 * len(ratios)) and
                      sorted(ratios)[1] > 1 + threshold else "inconclusive")
        rows.append({"case": {k: runs[0][key].get(k) for k in r.IDENTITY},
                     "before": old, "after": new, "block_ratios": ratios,
                     "ratio_mad": mad, "ratio_spread": spread,
                     "change_percent": 100 * (center - 1), "status": status})
    status = ("regression" if any(x["status"] == "regression" for x in rows) else
              "inconclusive" if any(x["status"] == "inconclusive" for x in rows) else "pass")
    return {"schema": 1, "protocol": manifest["protocol"], "status": status,
            "threshold": threshold, "noise": noise, "blocks": manifest["blocks"],
            "machine": manifest["machine"], "versions": manifest["versions"],
            "implementation_changes": manifest["implementation_changes"], "cases": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    p = sub.add_parser("capture")
    for role in ("before", "after"):
        p.add_argument(f"--{role}-build", type=Path, required=True)
        p.add_argument(f"--{role}-revision", required=True)
    p.add_argument("--suite", choices=r.SUITES, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--machine-id", required=True)
    p.add_argument("--cpu", type=int, required=True)
    p.add_argument("--serial")
    p.add_argument("--push-timeout", type=int, default=60)
    p.add_argument("--blocks", type=int, default=5)
    p = sub.add_parser("compare")
    p.add_argument("session", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--threshold", type=float, default=0.05)
    p.add_argument("--noise", type=float, default=0.03)
    args = parser.parse_args()
    try:
        if args.action == "capture":
            if not 1 <= args.push_timeout <= 600:
                raise ValueError("push timeout must be in 1..600 seconds")
            capture(args)
            return 0
        result = compare(args.session, args.threshold, args.noise)
        with args.output.open("x") as out:
            out.write(json.dumps(result, indent=2) + "\n")
        counts = {s: sum(x["status"] == s for x in result["cases"])
                  for s in ("pass", "regression", "inconclusive")}
        print(json.dumps({"status": result["status"], "counts": counts}))
        return {"pass": 0, "regression": 1, "inconclusive": 2}[result["status"]]
    except subprocess.CalledProcessError as error:
        print(f"benchmark command failed: exit {error.returncode}", file=sys.stderr)
        return 77 if error.returncode == 77 else 2
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
