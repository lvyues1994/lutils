"""Paired performance protocol tests with private JSON fixtures, no benchmarks."""
import copy
import json
import os
from pathlib import Path
import sys
import signal
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock

sys.dont_write_bytecode = True
import pair
import prepare_pair


class PairedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lutils-paired-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def build(self):
        unit = {"compiler": {"version": "test-compiler", "target": "x86_64"},
                "flags": ["-O3", "-std=c++17"]}
        return {"settings": {"CMAKE_BUILD_TYPE": "Release"}, "harness": {"main.cpp": "sha"},
                "units": {"benchmarks/simd/main.cpp": copy.deepcopy(unit),
                          "simd/src/old.cpp": copy.deepcopy(unit)}, "links": {"test": "old.o"}}

    def session(self, ratios=None, drift=1.0):
        path = self.root / "capture"
        path.mkdir()
        env = {"type": "environment", "schema": 1, "smoke": False, "threads": 1,
               "build": "Release", "seed": 1, "clock": "steady_clock"}
        info = self.build()
        manifest = {"schema": 1, "protocol": "abba-v1", "complete": True, "blocks": 5,
                    "machine": {"affinity": [2]}, "environment": env, "runs": [],
                    "versions": {side: {"revision": side, "build": copy.deepcopy(info)}
                                 for side in ("before", "after")},
                    "implementation_changes": pair.compatibility(info, info)}
        for i, (block, position, role) in enumerate(pair.schedule(5)):
            ns = round(100000 * drift**i * ((ratios or [1] * 5)[block] if role == "after" else 1))
            case = {"type": "case", "validated": True, "operation": "filter", "target": "avx2",
                    "scope": "kernel", "elements": 128, "warmups": 5 + i, "warmup_ns": 50000000,
                    "median_ns": ns, "min_ns": ns, "max_ns": ns, "mad_ns": 0,
                    "samples": [{"iterations": 1, "elapsed_ns": ns,
                                 "cpu_before": 2, "cpu_after": 2} for _ in range(9)]}
            file = path / f"{i}.jsonl"
            file.write_text("\n".join(json.dumps(x) for x in (env, case, {"type": "complete"})))
            manifest["runs"].append({"block": block, "position": position, "role": role,
                                     "file": file.name, "sha256": pair.r.digest(file),
                                     "started_ns": 10 * i + 1, "ended_ns": 10 * i + 2})
        self.save(path, manifest)
        return path

    @staticmethod
    def save(path, data):
        (path / "manifest.json").write_text(json.dumps(data))

    def mutate(self, path, fn):
        data = json.loads((path / "manifest.json").read_text())
        fn(data)
        self.save(path, data)

    def test_private_implementation_changes_allowed(self):
        a, b = self.build(), self.build()
        b["units"]["simd/src/new.cpp"] = b["units"].pop("simd/src/old.cpp")
        b["units"]["simd/src/new.cpp"]["flags"].append("-funroll-loops")
        b["links"]["test"] = "new.o"
        changes = pair.compatibility(a, b)
        self.assertEqual(changes["added_units"], ["simd/src/new.cpp"])
        self.assertEqual(changes["removed_units"], ["simd/src/old.cpp"])
        self.assertTrue(changes["link_changes"])
        b = self.build()
        b["units"]["simd/src/old.cpp"]["flags"].append("-funroll-loops")
        self.assertIn("simd/src/old.cpp", pair.compatibility(a, b)["changed_units"])

    def test_measurement_changes_rejected(self):
        for key in ("settings", "harness", "flags", "compiler"):
            a, b = self.build(), self.build()
            if key in ("settings", "harness"):
                b[key]["different"] = "value"
            elif key == "flags":
                b["units"]["benchmarks/simd/main.cpp"][key].append("-ffast-math")
            else:
                b["units"]["simd/src/old.cpp"][key]["version"] = "new compiler"
            with self.assertRaises(ValueError):
                pair.compatibility(a, b)

    def test_common_drift_cancels(self):
        result = pair.compare(self.session(drift=1.04))
        self.assertEqual(result["status"], "pass")
        self.assertAlmostEqual(result["cases"][0]["change_percent"], 0, places=3)

    def test_regression(self):
        self.assertEqual(pair.compare(self.session([1.1] * 5))["status"], "regression")

    def test_noise(self):
        self.assertEqual(pair.compare(self.session([1.0, 1.15, 1.3, 1.45, 1.6]))["status"],
                         "inconclusive")

    def test_boundary(self):
        self.assertEqual(pair.compare(self.session([1.049] * 5))["status"], "pass")

    def test_invalid_order_and_missing_block(self):
        path = self.session()
        original = json.loads((path / "manifest.json").read_text())
        mutations = [lambda m: m["runs"].pop(),
                     lambda m: m["runs"][0].update(role="after"),
                     lambda m: m["runs"][1].update(started_ns=0),
                     lambda m: m.update(complete=False),
                     lambda m: m.update(protocol=1)]
        for fn in mutations:
            data = copy.deepcopy(original)
            fn(data)
            self.save(path, data)
            with self.assertRaises(ValueError):
                pair.load(path)

    def test_corrupt_and_duplicate_runs(self):
        path = self.session()
        self.mutate(path, lambda m: m["runs"][1].update(file="0.jsonl"))
        with self.assertRaises(ValueError):
            pair.load(path)

    def test_case_or_affinity_change(self):
        path = self.session()
        original = (path / "0.jsonl").read_text()
        for field, value in (("source_stride", 999), ("cpu_before", 7)):
            rows = [json.loads(line) for line in original.splitlines()]
            if field == "source_stride":
                rows[1][field] = value
            else:
                rows[1]["samples"][0][field] = value
            (path / "0.jsonl").write_text("\n".join(json.dumps(x) for x in rows))
            self.mutate(path, lambda m: m["runs"][0].update(sha256=pair.r.digest(path / "0.jsonl")))
            with self.assertRaises(ValueError):
                pair.load(path)

    def test_symlink(self):
        path = self.session()
        (path / "0.jsonl").rename(self.root / "outside")
        (path / "0.jsonl").symlink_to(self.root / "outside")
        with self.assertRaises(ValueError):
            pair.load(path)

    @unittest.skipIf(sys.version_info < (3, 12), "safe source extraction requires Python 3.12+")
    def test_revision_validation_and_common_harness(self):
        repo = self.root / "repo"
        repo.mkdir()
        env = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
        def git(*args):
            return subprocess.check_output(["git", "-C", str(repo), "-c", "user.name=Fixture",
                "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
                "-c", "core.hooksPath=/dev/null", *args], env=env, text=True).strip()
        git("-c", "init.templateDir=", "init", "-q")
        harness = repo / "benchmarks/simd"
        harness.mkdir(parents=True)
        (harness / "old.hpp").write_text("old")
        git("add", ".")
        git("commit", "-qm", "before")
        before = git("rev-parse", "HEAD")
        (harness / "old.hpp").unlink()
        (harness / "current.hpp").write_text("current")
        git("add", "-A")
        git("commit", "-qm", "after")
        after = git("rev-parse", "HEAD")
        output = self.root / "prepared"
        with patch.object(prepare_pair, "run_bounded") as commands:
            self.assertEqual(prepare_pair.prepare(repo, before, after, output),
                             {"before": before, "after": after})
            self.assertEqual(commands.call_count, 6)
            self.assertEqual(commands.call_args_list[0].args[2], 60)
            self.assertIn("--no-tests=error", commands.call_args_list[2].args[0])
        before_harness = output / "before-source/benchmarks/simd"
        self.assertFalse((before_harness / "old.hpp").exists())
        self.assertEqual((before_harness / "current.hpp").read_text(), "current")
        failed = self.root / "failed-build"
        with patch.object(prepare_pair, "run_bounded",
                          side_effect=subprocess.CalledProcessError(1, "fixture")):
            with self.assertRaises(subprocess.CalledProcessError):
                prepare_pair.prepare(repo, before, after, failed)
        self.assertFalse((failed / "revisions.json").exists())
        for old, new in ((after, before), ("0" * 40, after), (after, after)):
            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                prepare_pair.revisions(repo, old, new)

    def test_timeout_kills_owned_group_even_after_parent_exit(self):
        process = Mock(pid=123456)
        process.wait.side_effect = [subprocess.TimeoutExpired("fixture", 1), 0, 0]
        with patch.object(prepare_pair.subprocess, "Popen", return_value=process) as create, \
                patch.object(prepare_pair.os, "killpg") as kill:
            with self.assertRaises(subprocess.TimeoutExpired):
                prepare_pair.run_bounded(["fixture"], None, 1)
            self.assertTrue(create.call_args.kwargs["start_new_session"])
            self.assertEqual(kill.call_args_list[0].args, (123456, signal.SIGTERM))
            self.assertEqual(kill.call_args_list[1].args, (123456, signal.SIGKILL))


if __name__ == "__main__":
    unittest.main()
