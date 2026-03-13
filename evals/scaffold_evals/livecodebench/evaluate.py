"""Score LiveCodeBench predictions by running code against test cases.

Loads the HuggingFace dataset (for private_test_cases), pairs each prediction
with its test cases, executes the code in a subprocess, and compares output.

Writes a JSON report with resolved/unresolved instance IDs, compatible with
the benchmark tracking in run_eval.py.
"""

from __future__ import annotations

import argparse
import base64
import json
import pickle
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


def _load_test_cases(
    dataset: str, version: str, qids: set[str],
) -> dict[str, list[dict]]:
    """Load test cases for specific question IDs from the HuggingFace dataset."""
    from datasets import load_dataset

    ds = load_dataset(
        dataset, version_tag=version, split="test",
        streaming=True, trust_remote_code=True,
    )

    cases: dict[str, list[dict]] = {}
    for item in ds:
        qid = str(item.get("question_id", ""))
        if qid not in qids:
            continue
        raw = item.get("private_test_cases", item.get("public_test_cases", ""))
        parsed = _decode_test_cases(raw)
        cases[qid] = parsed
        if len(cases) == len(qids):
            break
    return cases


def _decode_test_cases(raw) -> list[dict]:
    """Decode test cases from various formats (JSON string, pickle, list)."""
    if not raw:
        return []
    if isinstance(raw, list):
        return raw
    if isinstance(raw, str):
        # Try JSON first
        try:
            result = json.loads(raw)
            if isinstance(result, list):
                return result
        except (json.JSONDecodeError, TypeError):
            pass
        # Try base64 → zlib → pickle → JSON (LiveCodeBench private tests)
        try:
            decoded = base64.b64decode(raw)
            decompressed = zlib.decompress(decoded)
            unpickled = pickle.loads(decompressed)
            # Unpickled result is a JSON string, parse it
            if isinstance(unpickled, str):
                result = json.loads(unpickled)
                if isinstance(result, list):
                    return result
            elif isinstance(result, list):
                return [_test_to_dict(t) for t in unpickled]
        except Exception:
            pass
    return []


def _test_to_dict(test) -> dict:
    """Convert a test case object to a dict with input/output/testtype."""
    if isinstance(test, dict):
        return test
    # Handle dataclass or namedtuple
    return {
        "input": getattr(test, "input", ""),
        "output": getattr(test, "output", getattr(test, "expected_output", "")),
        "testtype": str(getattr(test, "testtype", "stdin")),
    }


def _run_code(code: str, stdin: str, timeout: int = 10) -> tuple[str, bool]:
    """Run Python code with stdin, return (stdout, success)."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".py", delete=False) as f:
        f.write(code)
        f.flush()
        try:
            result = subprocess.run(
                [sys.executable, f.name],
                input=stdin,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
            return result.stdout, result.returncode == 0
        except subprocess.TimeoutExpired:
            return "", False
        except Exception:
            return "", False


def _check_output(actual: str, expected: str) -> bool:
    """Compare outputs, stripping trailing whitespace per line."""
    actual_lines = [l.rstrip() for l in actual.strip().splitlines()]
    expected_lines = [l.rstrip() for l in expected.strip().splitlines()]
    return actual_lines == expected_lines


def score_predictions(
    predictions_path: Path,
    test_cases: dict[str, list[dict]],
    timeout: int = 10,
) -> dict[str, bool]:
    """Score each prediction against test cases. Returns {qid: passed}."""
    results: dict[str, bool] = {}

    with open(predictions_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            pred = json.loads(line)
            qid = str(pred.get("question_id", pred.get("instance_id", "")))
            code_list = pred.get("code_list", [])
            if not code_list or not code_list[0].strip():
                results[qid] = False
                print(f"  {qid}: FAIL (no code)", flush=True)
                continue

            code = code_list[0]
            cases = test_cases.get(qid, [])
            if not cases:
                print(f"  {qid}: SKIP (no test cases)", flush=True)
                continue

            passed = True
            for case in cases:
                inp = case.get("input", "")
                expected = case.get("output", case.get("expected_output", ""))
                stdout, ok = _run_code(code, inp, timeout=timeout)
                if not ok or not _check_output(stdout, expected):
                    passed = False
                    break

            results[qid] = passed
            status = "PASS" if passed else "FAIL"
            print(f"  {qid}: {status}", flush=True)

    return results


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Score LiveCodeBench predictions against test cases"
    )
    parser.add_argument("--predictions", required=True, help="Path to predictions JSONL")
    parser.add_argument(
        "--dataset", default="livecodebench/code_generation_lite",
        help="HuggingFace dataset name",
    )
    parser.add_argument(
        "--version", default="release_v6",
        help="Dataset version tag",
    )
    parser.add_argument("--output", default="report.json", help="Output report JSON")
    parser.add_argument(
        "--timeout", type=int, default=10,
        help="Per-test-case timeout in seconds",
    )
    args = parser.parse_args()

    # Read prediction qids first so we only load test cases we need
    pred_qids: set[str] = set()
    with open(args.predictions) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            pred = json.loads(line)
            pred_qids.add(str(pred.get("question_id", pred.get("instance_id", ""))))

    print(f"[livecodebench-eval] Loading test cases for {len(pred_qids)} predictions...", flush=True)
    test_cases = _load_test_cases(args.dataset, args.version, pred_qids)
    print(f"[livecodebench-eval] Loaded test cases for {len(test_cases)} problems", flush=True)

    print("[livecodebench-eval] Scoring predictions...", flush=True)
    results = score_predictions(Path(args.predictions), test_cases, args.timeout)

    resolved = [qid for qid, passed in results.items() if passed]
    unresolved = [qid for qid, passed in results.items() if not passed]

    report = {
        "resolved_ids": sorted(resolved),
        "unresolved_ids": sorted(unresolved),
        "total": len(results),
        "passed": len(resolved),
        "failed": len(unresolved),
    }

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2) + "\n")

    print(
        f"[livecodebench-eval] Done: {len(resolved)}/{len(results)} passed "
        f"({len(resolved)/len(results)*100:.1f}%)" if results else
        "[livecodebench-eval] No results",
        flush=True,
    )


if __name__ == "__main__":
    main()
