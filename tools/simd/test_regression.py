"""Offline tests: no benchmarks, devices, global state, or network access."""
import json
import importlib.util
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys
from unittest.mock import patch

sys.dont_write_bytecode = True
import regression as r


class RegressionTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="lutils-regression-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)

    def capture(self, name, ratio=1, offsets=None, selected="avx2"):
        path = self.root / name
        path.mkdir()
        env = {"type": "environment", "schema": 1, "smoke": False,
               "threads": 1, "build": "Release", "seed": 1, "clock": "steady_clock"}
        manifest = {"schema": 1, "protocol": 1, "complete": True, "machine_id": "test",
                    "machine": {"affinity": [2]}, "build": {"flags": "-O3 -std=c++17"},
                    "suite": "extended", "environment": env, "runs": []}
        for i, delta in enumerate(offsets or [0, 1, -1, 2, -2]):
            ns = int((10000 + delta) * ratio)
            case = {"type": "case", "validated": True, "operation": "copy_if",
                    "scope": "kernel", "target": "tuned", "elements": 128,
                    "selected_target": selected, "warmups": 5 + i, "warmup_ns": 50_000_000,
                    "median_ns": ns, "min_ns": ns, "max_ns": ns, "mad_ns": 0,
                    "samples": [{"iterations": 1, "elapsed_ns": ns,
                                 "cpu_before": 2, "cpu_after": 2} for _ in range(9)]}
            file = path / f"run-{i}.jsonl"
            file.write_text("\n".join(json.dumps(x) for x in (env, case, {"type": "complete"})))
            manifest["runs"].append({"file": file.name, "sha256": r.digest(file)})
        self.save(path, manifest)
        return path

    @staticmethod
    def save(path, manifest):
        (path / "manifest.json").write_text(json.dumps(manifest))

    def mutate(self, path, function):
        manifest = json.loads((path / "manifest.json").read_text())
        function(manifest)
        self.save(path, manifest)

    def mutate_case(self, path, function):
        def change(manifest):
            file = path / manifest["runs"][0]["file"]
            rows = [json.loads(line) for line in file.read_text().splitlines()]
            function(rows)
            file.write_text("\n".join(json.dumps(x) for x in rows))
            manifest["runs"][0]["sha256"] = r.digest(file)
        self.mutate(path, change)

    def test_pass_and_backend_change(self):
        result = r.compare(self.capture("a"), self.capture("b", 0.8, selected="neon"))
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["cases"][0]["after"]["selected_targets"], ["neon"] * 5)

    def test_implementation_identity(self):
        path = self.capture("implementations") / "run-0.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        rows[1]["implementation"] = "library"
        native = {**rows[1], "implementation": "intrinsic"}
        rows.insert(2, native)
        path.write_text("\n".join(json.dumps(x) for x in rows))
        self.assertEqual(len(r.load_run(path, 2)[1]), 2)
        rows[2]["implementation"] = "library"
        path.write_text("\n".join(json.dumps(x) for x in rows))
        with self.assertRaises(ValueError):
            r.load_run(path, 2)

    def test_regression(self):
        result = r.compare(self.capture("a"), self.capture("b", 1.1))
        self.assertEqual(result["status"], "regression")

    def test_noise_is_not_pass(self):
        result = r.compare(self.capture("a"), self.capture("b", offsets=[-3000, -2000, 0, 2000, 3000]))
        self.assertEqual(result["status"], "inconclusive")

    def test_threshold_boundary(self):
        result = r.compare(self.capture("a"), self.capture("b", 1.05))
        # 5% is the boundary, not a confirmed >5% regression.
        self.assertNotEqual(result["status"], "regression")

    def test_two_slow_runs_are_not_pass(self):
        result = r.compare(self.capture("a"),
                           self.capture("b", offsets=[0, 1, -1, 10000, 20000]))
        self.assertEqual(result["status"], "inconclusive")

    def test_incompatible(self):
        for key in ("machine_id", "machine", "build", "suite", "environment"):
            with self.subTest(key=key):
                a, b = self.capture("a" + key), self.capture("b" + key)
                self.mutate(b, lambda m: m.update({key: "changed"}))
                with self.assertRaises((ValueError, TypeError)):
                    r.compare(a, b)

    def test_case_inventory(self):
        a, b = self.capture("a"), self.capture("b")
        self.mutate_case(b, lambda rows: rows[1].update(elements=129))
        with self.assertRaises(ValueError):
            r.compare(a, b)

    def test_stride_identity(self):
        a, b = self.capture("a"), self.capture("b")
        self.mutate_case(b, lambda rows: rows[1].update(source_stride=512))
        with self.assertRaises(ValueError):
            r.compare(a, b)

    def test_missing_or_corrupt(self):
        for missing in (True, False):
            path = self.capture(str(missing))
            file = path / "run-0.jsonl"
            if missing:
                file.unlink()
            else:
                file.write_text("corrupt")
            with self.assertRaises((OSError, ValueError)):
                r.load_capture(path)

    def test_duplicate_or_escape(self):
        for name in ("../outside", "/tmp/outside", "run-0.jsonl"):
            path = self.capture(name.replace("/", "-"))
            self.mutate(path, lambda m: m["runs"][1].update(file=name))
            with self.assertRaises(ValueError):
                r.load_capture(path)

    def test_duplicate_contents(self):
        path = self.capture("a")
        (path / "run-1.jsonl").write_bytes((path / "run-0.jsonl").read_bytes())
        self.mutate(path, lambda m: m["runs"][1].update(sha256=m["runs"][0]["sha256"]))
        with self.assertRaises(ValueError):
            r.load_capture(path)

    def test_partial_capture(self):
        for key in ("complete", "runs"):
            path = self.capture(key)
            self.mutate(path, lambda m: m.update({key: False if key == "complete" else m[key][:4]}))
            with self.assertRaises(ValueError):
                r.load_capture(path)

    def test_bad_samples(self):
        invalid = [lambda rows: rows[0].update(smoke=True),
                   lambda rows: rows[0].update(type="unexpected"),
                   lambda rows: rows.pop(),
                   lambda rows: rows[1].update(validated=False),
                   lambda rows: rows[1].update(warmup_ns=1),
                   lambda rows: rows[1].update(warmup_ns=float("nan")),
                   lambda rows: rows[1].update(median_ns=float("nan")),
                   lambda rows: rows[1].update(median_ns=10**1000),
                   lambda rows: rows[1].update(calibration_ns=10**1000),
                   lambda rows: rows[1].update(calibration_ns=float("inf")),
                   lambda rows: rows[1]["samples"][0].update(iterations=0),
                   lambda rows: rows[1]["samples"][0].update(iterations=10**1000),
                   lambda rows: rows[1]["samples"][0].update(elapsed_ns=10**1000),
                   lambda rows: rows[1]["samples"][0].update(elapsed_ns=-1),
                   lambda rows: rows[1]["samples"][0].update(cpu_after=3),
                   lambda rows: rows[1]["samples"].pop()]
        for i, mutate in enumerate(invalid):
            with self.subTest(i=i):
                path = self.capture(str(i))
                self.mutate_case(path, mutate)
                with self.assertRaises((ValueError, ZeroDivisionError)):
                    r.load_capture(path)

    def test_invalid_manifest_type(self):
        path = self.capture("bad-manifest")
        self.save(path, [])
        with self.assertRaises(ValueError):
            r.load_capture(path)

    def test_invalid_threshold(self):
        for value in (0, -1, 1, float("nan")):
            with self.assertRaises(ValueError):
                r.compare(self.root, self.root, threshold=value)

    def test_symlink_and_affinity(self):
        path = self.capture("link")
        file = path / "run-0.jsonl"
        outside = self.root / "outside"
        file.rename(outside)
        file.symlink_to(outside)
        with self.assertRaises(ValueError):
            r.load_capture(path)
        for i, value in enumerate(([], [2, 3], [True], [-1], "2")):
            path = self.capture(str(i))
            self.mutate(path, lambda m: m["machine"].update(affinity=value))
            with self.assertRaises(ValueError):
                r.load_capture(path)

    def build_fixture(self, name, flags=None):
        source = self.root / name
        build = source / "build"
        build.mkdir(parents=True)
        (source / "benchmarks/simd").mkdir(parents=True)
        (source / "benchmarks/simd/fixture.cpp").write_text("same workload")
        (build / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\nCMAKE_BUILD_TYPE:STRING=Release\n"
            "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_MAKE_PROGRAM:FILEPATH=ninja\n")
        entry = {"file": "../simd/src/Dispatch.cpp", "directory": str(build),
                 "arguments": ["c++", "-I" + str(source / "simd/include"),
                               *(flags or ["-O3", "-std=c++17", "-mavx2", "-ffp-contract=off"]),
                               "-o", "output.o", "-c", "../simd/src/Dispatch.cpp"]}
        (build / "compile_commands.json").write_text(json.dumps([entry]))
        return build

    def test_build_fingerprint(self):
        def fake_command(args, **_):
            if "--version" in args:
                return "compiler 1"
            if "-dumpmachine" in args:
                return "x86_64-test"
            return "c++ lib.a -o benchmarks/simd/" + args[-1]
        with patch.object(r, "command", side_effect=fake_command):
            a = r.build_info(self.build_fixture("a"))
            b = r.build_info(self.build_fixture("b"))
            self.assertEqual(a, b)
            for i, flags in enumerate((["-O2"], ["-std=c++20"], ["-mno-avx"],
                                       ["-ffp-contract=fast"], ["-DA=1", "-UA"], ["-UA", "-DA=1"])):
                value = r.build_info(self.build_fixture(str(i), flags))
                self.assertNotEqual(a, value)
            with self.assertRaises(ValueError):
                r.build_info(self.build_fixture("response", ["@options.rsp"]))
            path = self.build_fixture("duplicate")
            file = path / "compile_commands.json"
            file.write_text(json.dumps(json.loads(file.read_text()) * 2))
            with self.assertRaises(ValueError):
                r.build_info(path)

    def test_android_cleanup_keeps_primary_failure(self):
        spec = importlib.util.spec_from_file_location("android_runner", r.ROOT / "tools/android/run.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        binary = self.root / "fake-executable"
        binary.touch()
        for primary, expected in ((0, 2), (77, 77)):
            invocations = []
            def run(args, **kwargs):
                invocations.append(args)
                if args[-1].startswith("rm -rf /data/local/tmp/lutils-"):
                    raise subprocess.TimeoutExpired(args, 15)
                return subprocess.CompletedProcess(args, primary if args[-1].startswith("cd ") else 0)
            argv = ["run.py", "--serial", "fixture", "--cpu", "7", "--timeout", "50", "--", str(binary)]
            with patch.object(sys, "argv", argv), patch.object(runner.subprocess, "run", side_effect=run), \
                    patch.object(sys, "stderr"):
                self.assertEqual(runner.main(), expected)
            self.assertEqual(len(invocations), 5)
            self.assertIn("taskset 80 ./test", invocations[3][-1])


if __name__ == "__main__":
    unittest.main()
