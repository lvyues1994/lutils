"""Capture bounded fixed-CPU runs and compare independently repeated SIMD measurements."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import statistics as stats
import subprocess
import sys

sys.dont_write_bytecode = True
from report import KEYS, read_run

SUITES = {
    "composition": ("simd_composition_benchmark", []),
    "primitives": ("simd_benchmark", []),
    "extended": ("simd_extended_benchmark", []),
    "filters": ("simd_extended_benchmark", ["--filters"]),
    **{name: ("simd_resource_benchmark", ["--memory", name])
       for name in ("va", "dma", "ahb", "ahb-rgba")},
}
IDENTITY = (*KEYS, "source_stride", "destination_stride")
ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args, timeout=15):
    return subprocess.run(args, check=True, capture_output=True, text=True,
                          timeout=timeout).stdout.strip()


def read_optional(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return "unavailable"


def build_info(build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    source = Path(cache["CMAKE_HOME_DIRECTORY"]).resolve()
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("capture requires a Release build")
    commands = json.loads((build / "compile_commands.json").read_text())
    units, compilers = {}, {}
    def normalize(word):
        for root, label in ((build, "<build>"), (source, "<source>")):
            word = re.sub(re.escape(str(root)) + r'''(?=/|$|[\s"'])''', label, word)
        return word
    for entry in commands:
        file = (Path(entry["directory"]) / entry["file"]).resolve()
        relative = file.relative_to(source).as_posix()
        if not relative.startswith(("simd/", "image/", "benchmarks/simd/", "core/",
                                    "compute/runtime/")):
            continue
        args = entry.get("arguments") or shlex.split(entry["command"])
        if any(word.startswith("@") for word in args):
            raise ValueError("response files are unsupported in the compile fingerprint")
        compiler = args[0]
        if compiler not in compilers:
            compilers[compiler] = {
                "version": normalize(command([compiler, "--version"])),
                "target": command([compiler, "-dumpmachine"]),
            }
        flags, i = [], 1
        while i < len(args):
            word = args[i]
            if word in ("-o", "-MF", "-MT", "-MQ"):
                i += 2
                continue
            if word not in ("-c", str(file), entry["file"]):
                flags.append(normalize(word))
            i += 1
        if relative in units:
            raise ValueError(f"duplicate compilation unit: {relative}")
        units[relative] = {"compiler": compilers[compiler], "flags": flags}
    if not units:
        raise ValueError("no SIMD compilation units")
    harness = {p.relative_to(source).as_posix(): digest(p)
               for p in sorted((source / "benchmarks/simd").glob("*")) if p.is_file()}
    settings = {k: normalize(v) for k, v in cache.items()
                if k.startswith(("CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS",
                                 "CMAKE_INTERPROCEDURAL_OPTIMIZATION")) or
                k in ("CMAKE_CXX_STANDARD", "CMAKE_CXX_EXTENSIONS", "ANDROID_ABI",
                      "ANDROID_PLATFORM", "ANDROID_STL", "LUTILS_ENABLE_SANITIZERS",
                      "LUTILS_ENABLE_TSAN", "LUTILS_ENABLE_VULKAN")}
    if cache.get("CMAKE_GENERATOR") != "Ninja":
        raise ValueError("capture currently requires the single-config Ninja generator")
    links = {}
    for target in sorted({binary for binary, _ in SUITES.values()}):
        commands = command([cache["CMAKE_MAKE_PROGRAM"], "-C", str(build),
                            "-t", "commands", target]).splitlines()
        if not commands or f"-o benchmarks/simd/{target}" not in commands[-1]:
            raise ValueError(f"cannot identify final link command for {target}")
        if any(word.startswith("@") or ",@" in word for word in shlex.split(commands[-1])):
            raise ValueError("response files are unsupported in the link fingerprint")
        links[target] = normalize(commands[-1])
    return {"units": units, "settings": settings, "harness": harness, "links": links}


def machine_info(cpu, serial):
    if serial:
        def read(path):
            return command(["adb", "-s", serial, "shell", "cat", path])
        def prop(name):
            return command(["adb", "-s", serial, "shell", "getprop", name])
        machine = {"platform": "android", "model": prop("ro.soc.model"),
                   "fingerprint": prop("ro.build.fingerprint"),
                   "kernel": command(["adb", "-s", serial, "shell", "uname", "-r"]),
                   "architecture": prop("ro.product.cpu.abi")}
    else:
        if platform.system() != "Linux" or cpu not in os.sched_getaffinity(0):
            raise ValueError("capture needs an available Linux CPU")
        read = read_optional
        machine = {"platform": "linux", "kernel": platform.release(),
                   "architecture": platform.machine()}
    info = read("/proc/cpuinfo")
    # Keep processor identity and ISA; never capture serial numbers or environment variables.
    machine["cpu"] = sorted({line.strip() for line in info.splitlines()
                             if line.split(":")[0].strip() in
                             ("model name", "flags", "Features", "CPU part", "microcode")})
    machine["governor"] = read(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor")
    machine["affinity"] = [cpu]
    return machine


def load_run(path, cpu):
    cases = read_run(path)
    environment = json.loads(path.read_text().splitlines()[0])
    if (environment.get("type") != "environment" or environment.get("smoke") is not False
            or environment.get("threads") != 1):
        raise ValueError("performance capture needs full single-threaded runs")
    if environment.get("build") != "Release" or not cases:
        raise ValueError("empty or non-Release run")
    result = {}
    for case in cases:
        if (type(case.get("warmups")) is not int or type(case.get("warmup_ns")) is not int
                or case["warmups"] < 1 or case["warmup_ns"] < 50_000_000):
            raise ValueError("insufficient warmup")
        for sample in case["samples"]:
            for key in ("elapsed_ns", "iterations"):
                value = sample[key]
                if type(value) is not int or value <= 0:
                    raise ValueError("invalid timing sample")
            if sample.get("cpu_before") != cpu or sample.get("cpu_after") != cpu:
                raise ValueError("CPU migration or incorrect affinity")
        for key in ("median_ns", "min_ns", "max_ns", "mad_ns", "calibration_ns"):
            value = case.get(key, 0)
            if (type(value) not in (int, float) or abs(value) > 2**63 - 1
                    or not math.isfinite(value) or value < 0):
                raise ValueError("nonfinite or negative measurement")
        key = json.dumps([case.get(k) for k in IDENTITY], separators=(",", ":"))
        if key in result:
            raise ValueError("duplicate case")
        result[key] = case
    return environment, result


def capture(args):
    if args.runs < 5 or args.runs > 20 or not 0 <= args.cpu <= 255:
        raise ValueError("use 5..20 runs and a CPU in 0..255")
    build = args.build.resolve()
    binary_name, options = SUITES[args.suite]
    binary = build / "benchmarks/simd" / binary_name
    info = build_info(build)
    machine = machine_info(args.cpu, args.serial)
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": 1, "protocol": 1, "machine_id": args.machine_id,
                "machine": machine, "build": info, "suite": args.suite,
                "binary_sha256": digest(binary), "revision": args.revision,
                "runs": [], "complete": False}
    path = args.output / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    environment, keys = None, None
    for n in range(args.runs):
        stem = f"run-{n:02}"
        output = args.output / (stem + ".jsonl")
        errors = args.output / (stem + ".stderr")
        invocation = [str(binary), *options]
        if args.serial:
            invocation = [sys.executable, str(ROOT / "tools/android/run.py"),
                          "--serial", args.serial, "--cpu", str(args.cpu), "--timeout", "50",
                          "--", *invocation]
        else:
            invocation = ["taskset", "-c", str(args.cpu), *invocation]
        with output.open("w") as out, errors.open("w") as err:
            # Android setup, default push, execution and cleanup are bounded (<=165s).
            completed = subprocess.run(invocation, stdout=out, stderr=err,
                                       timeout=195 if args.serial else 50)
        if completed.returncode:
            raise subprocess.CalledProcessError(completed.returncode, invocation)
        env, cases = load_run(output, args.cpu)
        if environment is not None and (environment != env or keys != set(cases)):
            raise ValueError("run environment/case inventory changed during capture")
        environment, keys = env, set(cases)
        manifest["runs"].append({"file": output.name, "sha256": digest(output)})
        print(f"{args.suite}: {n + 1}/{args.runs} ({len(cases)} validated cases)", flush=True)
    if machine_info(args.cpu, args.serial) != machine:
        raise ValueError("machine configuration changed during capture")
    if digest(binary) != manifest["binary_sha256"] or build_info(build) != info:
        raise ValueError("binary or build configuration changed during capture")
    manifest.update(complete=True, environment=environment)
    path.write_text(json.dumps(manifest, indent=2) + "\n")


def load_capture(path):
    manifest = json.loads((path / "manifest.json").read_text())
    if (not isinstance(manifest, dict) or manifest.get("schema") != 1 or manifest.get("protocol") != 1
            or manifest.get("complete") is not True):
        raise ValueError("incomplete capture or unsupported protocol")
    records = manifest["runs"]
    affinity = manifest["machine"]["affinity"]
    if (not isinstance(affinity, list) or len(affinity) != 1
            or type(affinity[0]) is not int or not 0 <= affinity[0] <= 255):
        raise ValueError("capture requires exactly one valid CPU")
    if not 5 <= len(records) <= 20:
        raise ValueError("capture needs at least five independent process runs")
    hashes, names, runs = set(), set(), []
    for record in records:
        name = record["file"]
        if Path(name).name != name or name in names:
            raise ValueError("invalid or duplicated run path")
        file = path / name
        if file.is_symlink() or file.resolve().parent != path.resolve() or not file.is_file():
            raise ValueError("run must be a regular file inside the capture directory")
        actual = digest(file)
        if actual != record["sha256"] or actual in hashes:
            raise ValueError("corrupt or duplicated run")
        env, cases = load_run(file, manifest["machine"]["affinity"][0])
        if env != manifest["environment"] or (runs and set(cases) != set(runs[0])):
            raise ValueError("run environment/cases differ")
        hashes.add(actual)
        names.add(name)
        runs.append(cases)
    return manifest, runs


def summary(cases):
    values = [case["median_ns"] for case in cases]
    center = stats.median(values)
    return {"median_ns": center,
            "between_mad_ratio": stats.median(abs(v - center) for v in values) / center,
            "between_spread_ratio": (sorted(values)[-2] - sorted(values)[1]) / center,
            "within_mad_ratio": max(case["mad_ns"] / case["median_ns"] for case in cases),
            "run_medians_ns": values,
            "selected_targets": [case.get("selected_target") for case in cases],
            "calibration_ns": [case.get("calibration_ns") for case in cases]}


def compare(before, after, threshold=0.05, noise=0.03):
    if not 0 < threshold < 1 or not 0 < noise < 1:
        raise ValueError("threshold and noise must be between zero and one")
    left, a = load_capture(before)
    right, b = load_capture(after)
    for key in ("protocol", "machine_id", "machine", "build", "suite", "environment"):
        if left[key] != right[key]:
            raise ValueError(f"incompatible capture: {key}")
    if len(a) != len(b) or set(a[0]) != set(b[0]):
        raise ValueError("different run counts or case inventories")
    rows = []
    for key in sorted(a[0]):
        old, new = summary([run[key] for run in a]), summary([run[key] for run in b])
        delta = new["median_ns"] / old["median_ns"] - 1
        noisy = any(s[k] > noise for s in (old, new)
                    for k in ("between_mad_ratio", "within_mad_ratio"))
        noisy = noisy or any(s["between_spread_ratio"] > 4 * noise for s in (old, new))
        losses = sum(v > old["median_ns"] * (1 + threshold) for v in new["run_medians_ns"])
        separated = (sorted(new["run_medians_ns"])[1] >
                     sorted(old["run_medians_ns"])[-2] * (1 + threshold))
        status = "inconclusive" if noisy else "pass"
        if not noisy and delta > threshold:
            status = "regression" if losses >= math.ceil(0.8 * len(b)) and separated else "inconclusive"
        rows.append({"case": {k: a[0][key].get(k) for k in IDENTITY}, "before": old,
                     "after": new, "change_percent": 100 * delta, "status": status})
    status = ("regression" if any(r["status"] == "regression" for r in rows) else
              "inconclusive" if any(r["status"] == "inconclusive" for r in rows) else "pass")
    return {"schema": 1, "status": status, "threshold": threshold, "noise": noise, "cases": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    p = sub.add_parser("capture")
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--suite", choices=SUITES, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--machine-id", required=True)
    p.add_argument("--revision", required=True, help="trace label; not a compatibility key")
    p.add_argument("--cpu", type=int, required=True)
    p.add_argument("--serial", help="Android ADB serial; omit for Linux")
    p.add_argument("--runs", type=int, default=5)
    p = sub.add_parser("compare")
    p.add_argument("before", type=Path)
    p.add_argument("after", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--threshold", type=float, default=0.05)
    p.add_argument("--noise", type=float, default=0.03)
    args = parser.parse_args()
    try:
        if args.action == "capture":
            capture(args)
            return 0
        result = compare(args.before, args.after, args.threshold, args.noise)
        with args.output.open("x") as output:
            output.write(json.dumps(result, indent=2) + "\n")
        counts = {name: sum(c["status"] == name for c in result["cases"])
                  for name in ("pass", "regression", "inconclusive")}
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
