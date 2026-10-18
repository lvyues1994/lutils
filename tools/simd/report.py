"""Validate SIMD benchmark JSONL and export case and sample CSV files (stdlib only)."""

import argparse
import csv
import json
import math
import pathlib
import statistics


def read_run(path):
    records = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if (not records or not all(isinstance(r, dict) for r in records)
            or records[0].get("type") != "environment" or records[0].get("schema") != 1
            or records[-1].get("type") != "complete"):
        raise ValueError(f"{path}: incomplete run or unsupported schema")
    cases = records[1:-1]
    seen = set()
    for case in cases:
        if case.get("type") != "case" or case.get("validated") is not True:
            raise ValueError(f"{path}: unvalidated case")
        key = tuple(case.get(name) for name in KEYS)
        if key in seen:
            raise ValueError(f"{path}: duplicate case {key}")
        seen.add(key)
        samples = case["samples"]
        expected = 1 if records[0]["smoke"] else 9
        if not isinstance(samples, list) or len(samples) != expected:
            raise ValueError(f"{path}: wrong sample count")
        times = []
        for sample in samples:
            if (not isinstance(sample, dict)
                    or type(sample.get("iterations")) is not int
                    or type(sample.get("elapsed_ns")) is not int
                    or not 0 < sample["iterations"] <= 2**63 - 1
                    or not 0 < sample["elapsed_ns"] <= 2**63 - 1):
                raise ValueError(f"{path}: invalid timing")
            times.append(sample["elapsed_ns"] / sample["iterations"])
        median = statistics.median(times)
        metrics = {"median_ns": median, "min_ns": min(times), "max_ns": max(times),
                   "mad_ns": statistics.median(abs(t - median) for t in times)}
        for name, expected in metrics.items():
            value = case[name]
            if (type(value) not in (int, float) or abs(value) > 2**63 - 1 or not math.isfinite(value)
                    or (value <= 0 if name != "mad_ns" else value < 0)
                    or not math.isclose(value, expected, rel_tol=1e-9, abs_tol=1e-6)):
                raise ValueError(f"{path}: inconsistent {name}")
    return cases


KEYS = ("operation", "target", "scope", "elements", "width", "height",
        "row_alignment", "element_offset", "positive_percent", "memory", "split")


def write_csv(path, fields, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=pathlib.Path, nargs="+")
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="prefix for .cases.csv and .samples.csv")
    args = parser.parse_args()
    cases, samples = [], []
    for path in args.inputs:
        for case in read_run(path):
            identity = {"run": path.stem, **{k: case.get(k, "") for k in KEYS}}
            identity.update({k: case.get(k, "") for k in
                             ("selected_target", "calibration_ns", "source_stride", "destination_stride")})
            cases.append({**identity, **{k: case[k] for k in
                          ("median_ns", "min_ns", "max_ns", "mad_ns")},
                          "million_elements_per_second": (
                              "" if case["scope"] == "map_finish" else
                              case["elements"] * 1000 / case["median_ns"])})
            for i, sample in enumerate(case["samples"]):
                samples.append({**identity, "sample": i, **sample,
                                "ns_per_call": sample["elapsed_ns"] / sample["iterations"]})
    if not cases:
        raise ValueError("no benchmark cases")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_csv(args.output.with_suffix(".cases.csv"), cases[0].keys(), cases)
    write_csv(args.output.with_suffix(".samples.csv"), samples[0].keys(), samples)
    print(f"Validated {len(cases)} cases, {len(samples)} samples")


if __name__ == "__main__":
    main()
